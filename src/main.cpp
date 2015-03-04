#include <xmpp/vaporo_component.hpp>
#include <steam/steam_client.hpp>
#include <network/poller.hpp>
#include <utils/timed_events.hpp>
#include <logger/logger.hpp>
#include <config/config.hpp>

/**
 * Provide an helpful message to help the user write a minimal working
 * configuration file.
 */
int config_help(const std::string& missing_option)
{
  if (!missing_option.empty())
    std::cerr << "Error: empty value for option " << missing_option << "." << std::endl;
  std::cerr <<
    "Please provide a configuration file filled like this:\n\n"
    "hostname=irc.example.com\npassword=S3CR3T\nsteam_login=example\nsteam_password=yoyo\nauthorized_jid=example@example.com"
            << std::endl;
  return 1;
}

int main(int ac, char** av)
{
  if (ac > 1)
    Config::filename = av[1];
  else
    Config::filename = "vaporo.cfg";

  Config::file_must_exist = true;
  std::cerr << "Using configuration file: " << Config::filename << std::endl;

  std::string password;
  try { // The file must exist
    password = Config::get("password", "");
  }
  catch (const std::ios::failure& e) {
    return config_help("");
  }
  const std::string hostname = Config::get("hostname", "");
  const std::string login = Config::get("steam_login", "");
  const std::string steam_pass = Config::get("steam_password", "");
  const std::string authorized_jid = Config::get("authorized_jid", "");
  if (password.empty())
    return config_help("password");
  if (hostname.empty())
    return config_help("hostname");
  if (login.empty())
    return config_help("login");
  if (steam_pass.empty())
    return config_help("steam_password");
  if (authorized_jid.empty())
    return config_help("authorized_jid");

  auto p = std::make_shared<Poller>();

  auto xmpp_component =
      std::make_shared<VaporoComponent>(p, hostname, password,
                                        authorized_jid, login, steam_pass);
  xmpp_component->start();

  auto timeout = TimedEventsManager::instance().get_timeout();
  while (p->poll(timeout) != -1)
    {
      TimedEventsManager::instance().execute_expired_events();
      timeout = TimedEventsManager::instance().get_timeout();
    }
  return 0;
}
