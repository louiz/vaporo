#ifndef VAPORO_COMPONENT_HPP_INCLUDED
#define VAPORO_COMPONENT_HPP_INCLUDED

#include <xmpp/xmpp_component.hpp>
#include <xmpp/roster.hpp>
#include <steam/steam_client.hpp>
#include <map>

class Poller;

class VaporoComponent: public XmppComponent
{
public:
  VaporoComponent(std::shared_ptr<Poller> poller,
                  const std::string& hostname,
                  const std::string& secret,
                  const std::string& authorized_jid,
                  const std::string& steam_login,
                  const std::string& steam_password);
  ~VaporoComponent() = default;

  void on_steam_roster_item_changed(const RosterItem* item);
  void send_roster_push(const RosterItem* item);

  /**
   * Send a basic presence with a type and an optional status. From contains
   * the local part of the from jid, if it's empty it's just the gateway JID.
   * Show is optional as well.
   */
  void send_presence(const std::string& from, const std::string& type,
                     const std::string& status_msg, const std::string& to,
                     const std::string& show);
  void send_message_from_steam(const std::string& from, const std::string& body);
  /**
   * Send a simple message from the gateway itself, to indicate an error, or
   * some other useful information to the user.
   */
  void send_information_message(const std::string& message);

  void on_roster_items_received(const XmlNode* node);

  /**
   * Handle the various stanza types
   */
  void handle_presence(const Stanza& stanza);
  void handle_message(const Stanza& stanza);
  void handle_iq(const Stanza& stanza);

  void after_handshake() override final;

  void shutdown();

private:
  SteamClient steam;

  std::string authorized_jid;
  /**
   * A roster containing the information we get from the XMPP server.
   */
  Roster xmpp_roster;
  /**
   * A roster containing the information we get from Steam.  We use it to
   * check differences with the xmpp_roster, and we send roster push to
   * remove these differences.
   */
  Roster steam_roster;

  VaporoComponent(const VaporoComponent&) = delete;
  VaporoComponent(VaporoComponent&&) = delete;
  VaporoComponent& operator=(const VaporoComponent&) = delete;
  VaporoComponent& operator=(VaporoComponent&&) = delete;
};


#endif /* VAPORO_COMPONENT_HPP_INCLUDED */
