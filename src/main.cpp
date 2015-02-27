#include <steam/steam_client.hpp>
#include <network/poller.hpp>
#include <utils/timed_events.hpp>

int main()
{
  auto p = std::make_shared<Poller>();
  SteamClient client(p);
  client.start();

  auto timeout = TimedEventsManager::instance().get_timeout();
  while (p->poll(timeout) != -1)
    {
      TimedEventsManager::instance().execute_expired_events();
      log_debug("poll");
      timeout = TimedEventsManager::instance().get_timeout();
      log_debug("Timeout: " << timeout.count());
    }
  return 0;
}
