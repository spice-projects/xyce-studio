#include "probe_names.h"

// network-parameter probes are S/Z/Y/H followed by two digits, e.g. Z11, Y21
bool is_network_parameter_probe_name(std::string_view name) {
    // match exactly three characters
    if (name.size() != 3) {
        return false;
    }
    // first character must be one of the network parameter families
    if (name[0] != 's' && name[0] != 'z' && name[0] != 'y' && name[0] != 'h') {
        return false;
    }
    // remaining two characters must be digits
    return name[1] >= '0' && name[1] <= '9' && name[2] >= '0' && name[2] <= '9';
}
