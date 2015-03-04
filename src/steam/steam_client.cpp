#include <steam/steam_client.hpp>
#include <logger/logger.hpp>
#include <network/poller.hpp>
#include <utils/timed_events.hpp>
#include <xmpp/vaporo_component.hpp>

#include <cstring>
#include <functional>
#include <fstream>

using namespace std::string_literals;

static const char* error_messages[] = {
  "",
  "",
  "generic failure",
  "no/failed network connection",
  "unknown error",
  "password/ticket is invalid",
  "same user logged in elsewhere",
  "protocol version is incorrect",
  "a parameter is incorrect",
  "file was not found",
  "called method busy - action not taken",
  "called object was in an invalid state",
  "name is invalid",
  "email is invalid",
  "name is not unique",
  "access is denied",
  "operation timed out",
  "VAC2 banned",
  "account not found",
  "steamID is invalid",
  "The requested service is currently unavailable",
  "The user is not logged on",
  "Request is pending (may be in process, or waiting on third party)",
  "Encryption or Decryption failed",
  "Insufficient privilege",
  "Too much of a good thing",
  "Access has been revoked (used for revoked guest passes)",
  "License/Guest pass the user is trying to access is expired",
  "Guest pass has already been redeemed by account, cannot be acked again",
  "The request is a duplicate and the action has already occurred in the past, ignored this time",
  "All the games in this guest pass redemption request are already owned by the user",
  "IP address not found",
  "failed to write change to the data store",
  "failed to acquire access lock for this operation",
  "unknown error",
  "unknown error",
  "unknown error",
  "unknown error",
  "unknown error",
  "failed to find the shopping cart requested",
  "a user didn't allow it",
  "target is ignoring sender",
  "nothing matching the request found",
  "unknown error",
  "this service is not accepting content changes right now",
  "account doesn't have value, so this feature isn't available",
  "allowed to take this action, but only because requester is admin",
  "A Version mismatch in content transmitted within the Steam protocol.",
  "The current CM can't service the user making a request, user should try another.",
  "You are already logged in elsewhere, this cached credential login has failed.",
  "You are already logged in elsewhere, you must wait",
  "unknown error",
  "unknown error",
  "unknown error",
  "unknown error",
  "unknown error",
};

static const char* steam_state_to_xmpp_show[] = {
  // See EPersonaState
  "",
  "",
  "dnd",
  "away",
  "xa",
  "chat",
  "chat"
};

SteamClient::SteamClient(std::shared_ptr<Poller> poller,
                         const std::string& login,
                         const std::string& password):
  TCPSocketHandler(poller),
  login(login),
  password(password),
  sentry{},
  xmpp(nullptr)
{
  this->load_sentry();
  this->steam = std::make_unique<SteamPPClient>(
     [this](std::size_t length, std::function<void(unsigned char* buffer)> fill_my_buffer)
     {
       log_debug("Steam client wants to write " << length << " bytes");
       unsigned char buffer[length];
       fill_my_buffer(buffer);
       this->send_data({reinterpret_cast<char*>(buffer), length});
     },

     [this](std::function<void()> callback, int timeout)
     {
       log_debug("set_interval called, timeout = " << timeout);
       TimedEvent keepalive(std::chrono::seconds(timeout),
                  [callback]()
                  {
                    log_debug("Calling the interval callback stuff");
                    callback();
                  });
       TimedEventsManager::instance().add_event(std::move(keepalive));
     });
  this->steam->onHandshake = std::bind(&SteamClient::on_handshake, this);
  this->steam->onLogOn = std::bind(&SteamClient::on_log_on, this,
                                   std::placeholders::_1, std::placeholders::_2);
  this->steam->onSentry = std::bind(&SteamClient::on_sentry, this,
                                    std::placeholders::_1);
  this->steam->onRelationships = std::bind(&SteamClient::on_relationships, this,
                                           std::placeholders::_1,
                                           std::placeholders::_2,
                                           std::placeholders::_3);
  this->steam->onUserInfo = std::bind(&SteamClient::on_user_info, this,
                                      std::placeholders::_1, std::placeholders::_2,
                                      std::placeholders::_3, std::placeholders::_4,
                                      std::placeholders::_5, std::placeholders::_6);
  this->steam->onPrivateMsg = std::bind(&SteamClient::on_private_msg, this,
                                        std::placeholders::_1, std::placeholders::_2);
}

void SteamClient::start()
{
  this->connect("72.165.61.174", "27017", false);
}

void SteamClient::on_connected()
{
  log_debug("We are connected, calling steam->connected()");
  this->wanted_size = this->steam->connected();
  log_debug("done, wanted_size: " << this->wanted_size);
}

void SteamClient::on_connection_failed(const std::string& reason)
{
  log_debug("Connection failed: " << reason);
}

void SteamClient::on_connection_close(const std::string& error)
{
  log_debug("Connection closed: " << error);
}

void SteamClient::parse_in_buffer(const size_t size)
{
  log_debug("Data received: " << size);
  log_debug("We have: " << this->in_buf.size() << " and steam wants " << this->wanted_size);
  if (this->in_buf.size() >= this->wanted_size)
    {
      // Maybe do not get the to_send, just use the whole in_buf
      auto to_send = this->in_buf.substr(0, this->wanted_size);
      this->in_buf = this->in_buf.substr(this->wanted_size);
      this->wanted_size = this->steam->readable(reinterpret_cast<const unsigned char*>(to_send.data()));
      log_debug("New wanted_size: " << this->wanted_size);
      if (this->in_buf.size() >= this->wanted_size)
        this->parse_in_buffer(0);
    }
}

void SteamClient::on_handshake()
{
  log_debug("onHandshake");
  if (this->sentry[0] != '\0')
    {
      log_debug("using the sentry");
      this->steam->LogOn(this->login.data(), this->password.data(), reinterpret_cast<const unsigned char*>(this->sentry));
    }
  else
    {
      log_debug("Not using the sentry");
      this->steam->LogOn(this->login.data(), this->password.data(), nullptr, "2BP2N");
    }
  log_debug("LogOn() called");
}

void SteamClient::on_log_on(Steam::EResult result, Steam::SteamID steam_id)
{
  log_debug("on_log_on: " << static_cast<std::size_t>(result) << " steamid: " << steam_id.steamID64);
  if (result == Steam::EResult::OK)
    {
      // TODO: handle busy, away, etc
      this->roster.clear();
      this->steam->SetPersonaState(Steam::EPersonaState::Online);
      this->xmpp->send_presence({}, {}, {}, {}, {});
    }
  else
    {
      this->xmpp->send_presence({}, "unavailable", {}, {}, {});
      this->xmpp->send_information_message("Login failed: "s + error_messages[static_cast<std::size_t>(result)]);
    }
}

void SteamClient::on_sentry(const unsigned char* hash)
{
  log_debug("on_sentry");
  ::memcpy(this->sentry, hash, 20);
  log_debug("Sentry: " << std::string(reinterpret_cast<const char*>(hash), 20));
  this->save_sentry();
}

void SteamClient::on_relationships(bool incremental,
                                   std::map<Steam::SteamID, Steam::EFriendRelationship>& users,
                                   std::map<Steam::SteamID, Steam::EClanRelationship>& groups)
{
  log_debug("on_relationships: " << incremental);

  Steam::SteamID users_info[users.size()];
  log_debug("-- Friends --");
  std::size_t i = 0u;
  for (auto it = users.begin(); it != users.end(); ++it)
    {
      const Steam::SteamID& id = it->first;
      const Steam::EFriendRelationship& relationship = it->second;
      log_debug("SteamID: " << id.steamID64 << " with type " << static_cast<int>(relationship));
      users_info[i++] = id;
    }
  log_debug("Requesting user info for " << i << " friends");
  this->steam->RequestUserInfo(i, users_info);

  return ;
  // TODO, or remove
  log_debug("-- Groups --");
  for (auto it = groups.begin(); it != groups.end(); ++it)
    {
      const Steam::SteamID& id = it->first;
      const Steam::EClanRelationship& relationship = it->second;
      log_debug("SteamID: " << id.steamID64 << " with type " << static_cast<int>(relationship));
    }
}

void SteamClient::on_user_info(Steam::SteamID user, Steam::SteamID* source, const char* name,
                               Steam::EPersonaState* state, const unsigned char avatar_hash[20],
                               const char* game_name)
{
  const std::string id(std::to_string(user.steamID64));
  auto item = this->roster.get_item(id);
  if (!item)
    {
      std::string str_name;
      if (name)
        str_name = name;
      std::vector<std::string> groups;
      item = this->roster.add_item(id, str_name, groups);
    }
  else
    {
      if (name)
        item->name = name;
    }
  log_debug("on_user_info: " << name << ": " << user.steamID64);

  this->xmpp->on_steam_roster_item_changed(item);

  if (!state || *state == Steam::EPersonaState::Offline)
    this->xmpp->send_presence(id, "unavailable", {}, {}, {});
  else
    this->xmpp->send_presence(id, {}, {}, {},
                              steam_state_to_xmpp_show[static_cast<std::size_t>(*state)]);
  // TODO gaming PEP
  if (game_name)
    {
      log_debug("Game name: " << game_name);
    }
}

void SteamClient::on_private_msg(Steam::SteamID user, const char* message)
{
  log_debug("on_private_msg: " << user.steamID64 << " [" << message << "]");
  const std::string id = std::to_string(user.steamID64);
  this->xmpp->send_message_from_steam(id, message);
}

void SteamClient::save_sentry()
{
  std::ofstream sentry_file("./sentry.bin", std::ios::binary);
  sentry_file.write(reinterpret_cast<char*>(this->sentry), 20);
}

void SteamClient::load_sentry()
{
  std::ifstream sentry_file("./sentry.bin", std::ios::binary);
  if (!sentry_file.good())
    {
      log_debug("No sentry file found, or failed to open it, not loading any sentry");
    }
  else
    {
      log_debug("Loading sentry from file");
      sentry_file.read(reinterpret_cast<char*>(this->sentry), 20);
    }
}

void SteamClient::send_message(const std::string& str_id, const std::string& body)
{
  Steam::SteamID id(std::stoll(str_id));
  log_debug("sending steam message: " << id << " == " << id.steamID64 << " body: " << body);
  this->steam->SendPrivateMessage(id, body.data());
}
