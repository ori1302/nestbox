#include "host/protocol.h"
#include "util/json.h"

namespace nestbox {

const char* reason_phrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 202: return "Accepted";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 429: return "Too Many Requests";
        case 503: return "Service Unavailable";
        case 507: return "Insufficient Storage";
        default: break;
    }
    return status < 300 ? "OK" : status < 400 ? "Redirect" : status < 500 ? "Client Error" : "Server Error";
}

Response too_large_reply(size_t limit) {
    Response r;
    set_error(r, 413, "body over " + std::to_string(limit) + " bytes");
    return r;
}

TxtRecord bonjour_txt(const std::string& pet, const std::string& id, const std::string& mdns_host) {
    return {{"pet", pet}, {"api", "1"}, {"id", id}, {"host", mdns_host}};
}

std::string button_input(const std::string& press) {
    return JsonWriter().begin_object().field("press", press).end_object().text();
}

std::string sensor_input(const std::string& gesture) {
    return JsonWriter().begin_object().field("gesture", gesture).end_object().text();
}

std::string computer_input(bool asleep) {
    return JsonWriter().begin_object().field("computer", asleep ? "asleep" : "awake").end_object().text();
}

}  // namespace nestbox
