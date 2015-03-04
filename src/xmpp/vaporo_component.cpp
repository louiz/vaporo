#include <xmpp/vaporo_component.hpp>
#include <network/poller.hpp>
#include <logger/logger.hpp>
#include <xmpp/jid.hpp>
#include <utils/scopeguard.hpp>

VaporoComponent::VaporoComponent(std::shared_ptr<Poller> poller,
                                 const std::string& hostname,
                                 const std::string& secret,
                                 const std::string& authorized_jid,
                                 const std::string& steam_login,
                                 const std::string& steam_password):
  XmppComponent(poller, hostname, secret),
  steam(poller, steam_login, steam_password),
  authorized_jid(authorized_jid)
{
  this->steam.set_xmpp(this);
  this->stanza_handlers.emplace("presence",
                                std::bind(&VaporoComponent::handle_presence, this,std::placeholders::_1));
  this->stanza_handlers.emplace("message",
                                std::bind(&VaporoComponent::handle_message, this,std::placeholders::_1));
  this->stanza_handlers.emplace("iq",
                                std::bind(&VaporoComponent::handle_iq, this,std::placeholders::_1));
}

void VaporoComponent::handle_presence(const Stanza& stanza)
{
  const std::string from_str = stanza.get_tag("from");

  const Jid from(stanza.get_tag("from"));

  const std::string type = stanza.get_tag("type");
  if (type == "subscribe")
    { // User wants to add us in its roster
      if (from_str == this->authorized_jid)
        { // Auto-accept
          this->send_presence({}, "subscribed", {}, from_str, {});
          this->send_presence({}, "subscribe", {}, from_str, {});
        }
      else
        { // Auto-deny
          this->send_presence({}, "unsubscribed", {}, from_str, {});
        }
    }
  else if (type == "unavailable")
    {
      // TODO log-off from steam
    }
  else
    {
      // TODO: Log-in to steam
      this->steam.start();
    }
}

void VaporoComponent::handle_message(const Stanza& stanza)
{
  std::string from = stanza.get_tag("from");
  std::string id = stanza.get_tag("id");
  std::string to_str = stanza.get_tag("to");
  std::string type = stanza.get_tag("type");

  if (from.empty())
    return;
  if (type.empty())
    type = "normal";

  XmlNode* body = stanza.get_child("body", COMPONENT_NS);
  Jid to(to_str);
  if (body && !body->get_inner().empty())
    this->steam.send_message(to.local, body->get_inner());
}

void VaporoComponent::handle_iq(const Stanza& stanza)
{
  std::string id = stanza.get_tag("id");
  std::string from = stanza.get_tag("from");
  std::string to_str = stanza.get_tag("to");
  std::string type = stanza.get_tag("type");

  if (from.empty())
    return;
  if (id.empty() || to_str.empty() || type.empty())
    {
      this->send_stanza_error("iq", from, this->served_hostname, id,
                              "modify", "bad-request", "");
      return;
    }
  Jid to(to_str);

  // These two values will be used in the error iq sent if we don't disable
  // the scopeguard.
  std::string error_type("cancel");
  std::string error_name("internal-server-error");
  utils::ScopeGuard stanza_error([&](){
      this->send_stanza_error("iq", from, to_str, id,
                              error_type, error_name, "");
    });

  if (type == "result")
    {
      XmlNode* query;
      if ((query = stanza.get_child("query", "jabber:iq:roster")))
        { // We received the user's current roster
          this->on_roster_items_received(query);
        }
    }
  stanza_error.disable();
}

void VaporoComponent::send_presence(const std::string& from,
                                    const std::string& type,
                                    const std::string& status_msg,
                                    const std::string& to,
                                    const std::string& show)
{
  Stanza presence("presence");
  if (from.empty())
    presence["from"] = this->served_hostname;
  else
    presence["from"] = from + "@" + this->served_hostname;

  if (to.empty())
    presence["to"] = this->authorized_jid;
  else
    presence["to"] = to + "@" + this->served_hostname;

  if (!type.empty())
    presence["type"] = type;

  if (!status_msg.empty())
    {
      XmlNode status("status");
      status.set_inner(status_msg);
      status.close();
      presence.add_child(std::move(status));
    }

  if (!show.empty())
    {
      XmlNode show_elem("show");
      show_elem.set_inner(show);
      show_elem.close();
      presence.add_child(std::move(show_elem));
    }
  presence.close();
  this->send_stanza(presence);
}

void VaporoComponent::send_information_message(const std::string& txt)
{
  Stanza message("message");
  message["to"] = this->authorized_jid;
  message["type"] = "chat";
  XmlNode body("body");
  body.set_inner(txt);
  body.close();
  message.add_child(std::move(body));
  message.close();
  this->send_stanza(message);
}

void VaporoComponent::after_handshake()
{
  // Empty our internal roster
  this->xmpp_roster.clear();

  // Get the user's roster
  Stanza iq("iq");
  iq["to"] = this->authorized_jid;
  iq["type"] = "get";
  XmlNode query("jabber:iq:roster:query");
  query.close();
  iq.add_child(std::move(query));
  iq["id"] = this->next_id();
  iq.close();
  this->send_stanza(iq);
}

void VaporoComponent::on_roster_items_received(const XmlNode* node)
{
  log_debug("on_roster_items_received");
  auto items = node->get_children("item", "jabber:iq:roster");
  for (const auto item: items)
    {
      auto it = this->xmpp_roster.get_item(item->get_tag("jid"));
      if (!it)
        this->xmpp_roster.add_item(item->get_tag("jid"), item->get_tag("name"));
      else
        it->name = item->get_tag("name");
    }
}

void VaporoComponent::on_steam_roster_item_changed(const RosterItem* item)
{
  auto it = this->xmpp_roster.get_item(item->jid + "@" + this->served_hostname);
  if (!it || it->name != item->name)
    // The steam contact changed its name or it's a new contact, update the
    // item on the server roster
    this->send_roster_push(item);
}

void VaporoComponent::send_roster_push(const RosterItem* roster_item)
{
  Stanza iq("iq");
  iq["to"] = this->authorized_jid;
  iq["id"] = this->next_id();
  iq["type"] = "set";
  XmlNode query("jabber:iq:roster:query");
  XmlNode item("item");
  item["jid"] = roster_item->jid + "@" + this->served_hostname;
  item["name"] = roster_item->name;
  // TODO subscription
  item["subscription"] = "both";
  // TODO groups
  item.close();
  query.add_child(std::move(item));
  query.close();
  iq.add_child(std::move(query));
  iq.close();
  this->send_stanza(iq);
}

void VaporoComponent::send_message_from_steam(const std::string& from, const std::string& body)
{
  this->send_message(from, std::make_tuple(body, nullptr), this->authorized_jid,
                     "chat", false);
}

void VaporoComponent::shutdown()
{
  // Send an unavailable presence for each contact
  for (const auto& item: this->xmpp_roster.get_items())
    {
      Jid jid(item.jid);
      this->send_presence(jid.local, "unavailable", "Gateway shutdown", {}, {});
    }
  this->send_presence({}, "unavailable", "Gateway shutdown", {}, {});
}

