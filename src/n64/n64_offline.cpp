/**
 * n64_offline.cpp -- the engine's online features on a console with no network.
 *
 * Online play, the add-on server and HTTPS downloads are out of scope for the
 * N64 port. These behave as the PC game does without a connection: the
 * operation fails and the engine's own error handling takes over.
 */

#include "../addon/manager_ui.hpp"
#include "../network_download_file.hpp"
#include "../log.hpp"

#include <stdexcept>

static lg::log_domain log_n64_offline("n64/offline");

bool manage_addons() {
    LOG_STREAM(err, log_n64_offline) << "add-ons need a network connection; none on the Nintendo 64";
    return false;
}

namespace network {
bool download(const std::string& url, const std::string&) {
    LOG_STREAM(err, log_n64_offline) << "cannot download " << url << ": no network on the Nintendo 64";
    return false;
}
void gui_download(const std::string& url, const std::string& local_path) {
    download(url, local_path);
}
} // namespace network
