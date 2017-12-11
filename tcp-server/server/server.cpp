#include <sstream>
#include <unistd.h>
#include "server.h"
#include "utils/sockutils.h"
#include "json/src/json.hpp"


void server::Server::create_server_socket() {
    auto &&server_d = socket(AF_INET, SOCK_STREAM, 0);
    if (server_d < 0) {
        std::cout <<  "Cannot open socket" << std::endl;
        std::exit(1);
    }
    int enable_options = 1;
    setsockopt(server_d, SOL_SOCKET, SO_REUSEADDR, &enable_options, sizeof(enable_options));

    sockaddr_in server_address{};
    bzero(&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(SERVER_PORT);
    auto &&bind_addr = reinterpret_cast<const sockaddr *>(&server_address);
    auto &&bind_stat = bind(server_d, bind_addr, sizeof(server_address));
    if (bind_stat < 0) {
        std::cout <<  "Cannot bind" << std::endl;
        std::exit(1);
    }
    if (!socket_utils::set_socket_nonblock(server_d)) {
        std::cout <<  "Cannot set server socket nonblock" << std::endl;
        std::exit(1);
    }
    auto &&listen_stat = listen(server_d, 2);
    if (listen_stat == -1) {
        std::cout <<  "set server socket listen error" << std::endl;
        std::exit(1);
    }
    server_socket = server_d;
}

void server::Server::close_client(int client_d) {
    std::unique_lock<std::mutex> lock(clients_mutex);
    if (clients.find(client_d) == clients.end())
        return;
    auto &&client = clients[client_d];
    clients.erase(client_d);
    epoll_ctl(epoll_descriptor, EPOLL_CTL_DEL, client_d, &client.event);
    close(client.descriptor);
    client.is_active = false;
    lock.unlock();
    std::cout <<  "Client  disconnected"<< client_d << std::endl;
}

void server::Server::accept_client() {
    sockaddr_in client_addr{};
    auto &&client_addr_in = reinterpret_cast<sockaddr *>(&client_addr);
    auto &&client_addr_len = static_cast<socklen_t>(sizeof(sockaddr));
    auto &&client_d = accept(server_socket, client_addr_in, &client_addr_len);
    if (client_d == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cout <<  "Accept failed" << std::endl;
        }
        return;
    }
    if (!socket_utils::set_socket_nonblock(client_d)) {
        std::cerr <<  "Cannot set client socket nonblock" << client_d<< std::endl;
        return;
    }
    epoll_event event{};
    event.data.fd = client_d;
    event.events = EPOLLIN;
    auto &&ctl_stat = epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, client_d, &event);
    if (ctl_stat == -1) {
        std::cerr <<  "epoll_ctl failed" << std::endl;
        return;
    }
    std::string client_info = inet_ntoa(client_addr.sin_addr);
    std::unique_lock<std::mutex> lock(clients_mutex);
    clients[client_d] = Client(client_d, event, client_info);
    lock.unlock();
    std::cout <<  "New connection from " << client_info << "on socketstd" << client_d << std::endl;
}

void server::Server::read_client_data(int client_id) {
    char read_buffer[MESSAGE_SIZE];
    auto &&count = read(client_id, read_buffer, MESSAGE_SIZE);
    if (count == -1 && errno == EAGAIN) return;
    if (count == -1) std::cout <<  "Error in read for socket" << client_id << std::endl;
    if (count <= 0) {
        close_client(client_id);
        return;
    }
    auto &&client = clients[client_id];
    client.receive_buffer.append(read_buffer, static_cast<unsigned long>(count));
}

void server::Server::handle_client_if_possible(int client_id) {
    auto &&client = clients[client_id];
    auto &&message_end = client.receive_buffer.find(MESSAGE_END);
    if (message_end != std::string::npos) {
        auto &&message = client.receive_buffer.substr(0, message_end);
        client.receive_buffer.erase(0, message_end + strlen(MESSAGE_END));
        workers.enqueue(&Server::process_client_message, this, message, client_id);
    }
}


void send_message(int client_id, std::string_view message) {
    auto &&send_stat = send(client_id, message.data(), message.length(), 0);
    if (send_stat == -1) {
        std::cout <<  "Error in send for id" << client_id << std::endl;
    }
}


void server::Server::process_client_message(std::string &message, int client_id) {
    std::cout <<  message << std::endl;
    std::string_view message_view(message);
    if (message_view.compare(0, MESSAGE_PREFIX_LEN, CMD_PREFIX) == 0) {
        message_view.remove_prefix(MESSAGE_PREFIX_LEN);
        process_client_command(message_view, client_id);
    } else if (message_view.compare(0, MESSAGE_PREFIX_LEN, TXT_PREFIX) == 0) {
        message_view.remove_prefix(MESSAGE_PREFIX_LEN);
        process_client_text(message_view, client_id);
    } else if (message_view.compare(0, MESSAGE_PREFIX_LEN, JSON_PREFIX) == 0) {
        message_view.remove_prefix(MESSAGE_PREFIX_LEN);
        process_client_json(message_view, client_id);
    } else {
        std::cout <<  "Client" << client_id << "Unknown message type" << message << std::endl;
        auto &&err_message = ERROR_PREFIX + std::string("Unknown message type") + MESSAGE_END;
        send_message(client_id, err_message);
    }
}


void server::Server::process_client_text(std::string_view text, int client_id) {
    std::cout <<  "Text from client" << client_id << ":" << text.data() << std::endl;
    auto &&to_send = std::string(text) + MESSAGE_END;
    send_message(client_id, to_send);
}


void server::Server::process_add_currency(std::string &currency, int client_id) {
    std::cout <<  "Client" << client_id << "add currency " << currency<< std::endl;
    auto &&status = database.add_currency(currency);
    if (status == 0) {
        auto &&response = TXT_PREFIX + std::string("Successfully add currency ") + currency + MESSAGE_END;
        send_message(client_id, response);
    } else if (status == 1) {
        auto &&err_message = ERROR_PREFIX + std::string("Currency already exists: ") + currency + MESSAGE_END;
        send_message(client_id, err_message);
    } else {
        auto &&err_message = ERROR_PREFIX + std::string("Database error") + MESSAGE_END;
        send_message(client_id, err_message);
    }
}

void server::Server::process_add_currency_value(std::string &currency, double value, int client_id) {
    std::cout <<  "Client" << client_id << "add currency " << currency<< "value "<< value << std::endl;
    auto &&status = database.add_currency_value(currency, value);
    if (status == 0) {
        auto &&response = TXT_PREFIX + std::string("Successfully add value for currency ") + currency + MESSAGE_END;
        send_message(client_id, response);
    } else if (status == 1) {
        auto &&err_message = ERROR_PREFIX + std::string("No such currency ") + currency + MESSAGE_END;
        send_message(client_id, err_message);
    } else {
        auto &&err_message = ERROR_PREFIX + std::string("Database error") + MESSAGE_END;
        send_message(client_id, err_message);
    }
}

void server::Server::process_del_currency(std::string &currency, int client_id) {
    std::cout <<  "Client" << client_id << "del currency " << currency<< std::endl;
    auto &&status = database.del_currency(currency);
    if (status == 0) {
        auto &&response = TXT_PREFIX + std::string("Successfully del currency ") + currency + MESSAGE_END;
        send_message(client_id, response);
    } else if (status == 1) {
        auto &&err_message = ERROR_PREFIX + std::string("No such currency ") + currency + MESSAGE_END;
        send_message(client_id, err_message);
    } else {
        auto &&err_message = ERROR_PREFIX + std::string("Database error") + MESSAGE_END;
        send_message(client_id, err_message);
    }
}

void server::Server::process_list_all_currencies(int client_id) {
    std::cout <<  "Client" << client_id << "list all currencies" <<  std::endl;
    nlohmann::json json_response;
    auto &&status = database.currency_list(json_response);
    if (status == 0) {
        auto &&response = JSON_PREFIX + json_response.dump() + MESSAGE_END;
        send_message(client_id, response);
    } else {
        auto &&err_message = ERROR_PREFIX + std::string("Database error") + MESSAGE_END;
        send_message(client_id, err_message);
    }
}

void server::Server::process_currency_history(std::string &currency, int client_id) {
    nlohmann::json json_response;
    auto &&status = database.currency_history(currency, json_response);
    if (status == 0) {
        auto &&response = JSON_PREFIX + json_response.dump() + MESSAGE_END;
        send_message(client_id, response);
    } else if (status == 1) {
        auto &&err_message = ERROR_PREFIX + std::string("No such currency ") + currency + MESSAGE_END;
        send_message(client_id, err_message);
    } else {
        auto &&err_message = ERROR_PREFIX + std::string("Database error") + MESSAGE_END;
        send_message(client_id, err_message);
    }
}

void server::Server::process_client_command(std::string_view command, int client_id) {
    std::cout <<  "Command from client " << client_id << ":" << command.data() << std::endl;
    if (command == "disconnect") {
        close_client(client_id);
    } else if (command == REQUEST_GET_ALL_CURRENCIES) {
        process_list_all_currencies(client_id);
    } else {
        std::cout <<  "Client " << client_id << "Unknown command " << command.data() << std::endl;
        auto &&err_message = ERROR_PREFIX + std::string("Unknown command") + MESSAGE_END;
        send_message(client_id, err_message);
    }
}


void server::Server::process_client_json(std::string_view json_string, int client_id) {
    std::cout <<  "Json from client " << client_id << ":" << json_string.data() << std::endl;
    try {
        auto &&client_json = nlohmann::json::parse(json_string);
        std::string request_type = client_json["type"];
        std::string currency = client_json["currency"];
        if (request_type == REQUEST_ADD_CURRENCY) {
            process_add_currency(currency, client_id);
        } else if (request_type == REQUEST_ADD_CURRENCY_VALUE) {
            double value = client_json["value"];
            process_add_currency_value(currency, value, client_id);
        } else if (request_type == REQUEST_DEL_CURRENCY) {
            process_del_currency(currency, client_id);
        } else if (request_type == REQUEST_GET_CURRENCY_HISTORY) {
            process_currency_history(currency, client_id);
        } else {
            std::cout <<  "Client " << client_id << "Unknown request type:" << request_type << std::endl;
            auto &&err_message = ERROR_PREFIX + std::string("Unknown request type") + MESSAGE_END;
            send_message(client_id, err_message);
        }

    } catch (nlohmann::json::parse_error &ex) {
        std::cout <<  ex.what() << "client" << json_string<< "Unknown request type:" << json_string.data()<< std::endl;
        auto &&err_message = ERROR_PREFIX + std::string("Incorrect json") + MESSAGE_END;
        send_message(client_id, err_message);
        return;
    }

}

void server::Server::epoll_loop() {
    epoll_descriptor = epoll_create(1);
    if (epoll_descriptor == -1) {
        std::cout <<  "Cannot create epoll descriptor" << std::endl;
        std::exit(1);
    }
    epoll_event event{};
    event.events = EPOLLIN | EPOLLHUP;
    event.data.fd = server_socket;
    auto &&ctl_stat = epoll_ctl(epoll_descriptor, EPOLL_CTL_ADD, server_socket, &event);
    if (ctl_stat == -1) {
        std::cout <<  "epoll_ctl failed" << std::endl;
        std::exit(1);
    }
    std::array<epoll_event, 10> events{};
    std::cout <<  "Server started on port" << SERVER_PORT<< std::endl;
    while (!terminate) {
        auto &&event_cnt = epoll_wait(epoll_descriptor, events.data(), 10, 1000);
        for (auto &&i = 0; i < event_cnt; ++i) {
            auto &&evt = events[i];
            if (evt.events & EPOLLERR) {
                std::cout <<  "Epoll error for socket" << evt.data.fd<< std::endl;
                close_client(evt.data.fd);
            }
            if (evt.events & EPOLLHUP) {
                std::cout <<  "client socket closed" << evt.data.fd<< std::endl;
                close_client(evt.data.fd);
            }
            if (evt.events & EPOLLIN) {
                if (evt.data.fd == server_socket) accept_client();
                else {
                    read_client_data(evt.data.fd);
                    handle_client_if_possible(evt.data.fd);
                }
            }
        }
    }
}

void server::Server::stop() {
    if (terminate) return;
    terminate = true;
    close(server_socket);
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

void server::Server::start() {
    terminate = false;
    server_thread = std::move(std::thread(&Server::epoll_loop, this));
}

bool server::Server::is_active() {
    return !terminate;
}

void server::Server::close_all_clients() {
    auto &&it = std::begin(clients);
    while (it != std::end(clients)) {
        close_client(it->first);
        it = std::begin(clients);
    }
}

std::string server::Server::list_clients() {
    std::stringstream out_string;
    out_string << "Clients connected:";
    for (auto && [client_id, client]: clients) {
        out_string << "\nid: " << client_id << " " << client.client_ip_addr;
    }
    return out_string.str();
}
