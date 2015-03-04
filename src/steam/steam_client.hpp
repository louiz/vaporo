#ifndef STEAM_CLIENT_HPP_INCLUDED
#define STEAM_CLIENT_HPP_INCLUDED

#include <network/tcp_socket_handler.hpp>
#include <xmpp/roster.hpp>

#include <steam++.h>

#include <memory>

class Poller;
class VaporoComponent;

class SteamClient: public TCPSocketHandler
{
  using SteamPPClient = Steam::SteamClient;
public:
  SteamClient(std::shared_ptr<Poller> poller, const std::string& login,
              const std::string& password);
  ~SteamClient() = default;

  void set_xmpp(VaporoComponent* xmpp)
  {
    this->xmpp = xmpp;
  }

  void start();

  void on_connected() override final;
  void on_connection_failed(const std::string& reason) override final;
  void on_connection_close(const std::string& error) override final;
  void parse_in_buffer(const size_t size) override final;
  void send_message(const std::string& id, const std::string& body);

  /**
   * Callback called by the steam object on some events
   */
  void on_handshake();
  void on_log_on(Steam::EResult result, Steam::SteamID steam_id);
  void on_sentry(const unsigned char* hash);
  void save_sentry();
  void load_sentry();
  void on_relationships(bool incremental,
                        std::map<Steam::SteamID, Steam::EFriendRelationship>& users,
                        std::map<Steam::SteamID, Steam::EClanRelationship>& groups);
  void on_user_info(Steam::SteamID user, Steam::SteamID* source, const char* name,
                    Steam::EPersonaState* state, const unsigned char avatar_hash[20],
                    const char* game_name);
  void on_private_msg(Steam::SteamID user, const char* message);

private:
  std::unique_ptr<SteamPPClient> steam;
  const std::string login;
  const std::string password;
  /**
   * The size wanted by steam in the next readable() call
   */
  std::size_t wanted_size;
  unsigned char sentry[20];
  VaporoComponent* xmpp;
  Roster roster;

  SteamClient(const SteamClient&) = delete;
  SteamClient(SteamClient&&) = delete;
  SteamClient& operator=(const SteamClient&) = delete;
  SteamClient& operator=(SteamClient&&) = delete;
};


#endif /* STEAM_CLIENT_HPP_INCLUDED */
