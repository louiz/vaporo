#include <steam/steam_client.hpp>
#include <logger/logger.hpp>
#include <network/poller.hpp>
#include <utils/timed_events.hpp>

#include <cstring>
#include <functional>
#include <fstream>

SteamClient::SteamClient(std::shared_ptr<Poller> poller):
  TCPSocketHandler(poller),
  sentry{}
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
}

void SteamClient::start()
{
  this->connect("146.66.152.12", "27017", false);
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
  // TODO read from the config file
  if (this->sentry[0] != '\0')
    this->steam->LogOn("", "", reinterpret_cast<const unsigned char*>(this->sentry));
  else
    this->steam->LogOn("", "", nullptr, "2BP2N");
  log_debug("LogOn() called");
}

void SteamClient::on_log_on(Steam::EResult result, Steam::SteamID steam_id)
{
  log_debug("on_log_on: " << static_cast<std::size_t>(result) << " steamid: " << steam_id.steamID64);
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
  log_debug("on_user_info: " << name);
  if (state)
    {
      log_debug("status: " << static_cast<int>(*state));
    }
  else
    {
      log_debug("offline");
    }
  if (game_name)
    {
      log_debug("Game name: " << game_name);
    }
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

