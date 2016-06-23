#include "connection.hpp"
#include "server.hpp"

using namespace server;

Connection::OnConnection Connection::on_connection_ = []{};

Connection::Connection(Server& serv, Connection_ptr conn, size_t idx)
  : server_(serv), conn_(conn), idx_(idx)
{
  conn_->read(BUFSIZE, OnData::from<Connection, &Connection::on_data>(this));
  conn_->onDisconnect(OnDisconnect::from<Connection, &Connection::on_disconnect>(this));
  conn_->onError(OnError::from<Connection, &Connection::on_error>(this));
  on_connection_();
  //conn_->onPacketDropped(OnPacketDropped::from<Connection, &Connection::on_packet_dropped>(this));
}

void Connection::on_data(buffer_t buf, size_t n) {
  printf("<%s> @on_data, size=%u\n", to_string().c_str(), n);
  // if it's a new request
  if(!request_) {
    try {
      request_ = std::make_shared<Request>(buf, n);
      // return early to read payload
      if((request_->method() == http::POST or request_->method() == http::PUT)
        and !request_->is_complete())
      {
        printf("<%s> POST/PUT: [ConLen (%u) > Payload (%u)] => Buffering\n",
          to_string().c_str(), request_->content_length(), request_->payload_length());
        return;
      }
    } catch(...) {
      printf("<%s> Error - exception thrown when creating Request???\n", to_string().c_str());
      close();
      return;
    }
  }
  // else we assume it's payload
  else {
    request_->add_body(request_->get_body() + std::string((const char*)buf.get(), n));
    // if we haven't received all data promised
    printf("<%s> Received payload - Expected: %u - Recv: %u\n",
      to_string().c_str(), request_->content_length(), request_->payload_length());
    if(!request_->is_complete())
      return;
  }

  request_->complete();

  printf("<%s> Complete Request: [%s] Payload (%u/%u B)\n",
    to_string().c_str(),
    request_->route_string().c_str(),
    request_->payload_length(),
    request_->content_length()
    );

  response_ = std::make_shared<Response>(conn_);
  server_.process(request_, response_);
  request_ = nullptr;
}

void Connection::on_disconnect(Connection_ptr, Disconnect reason) {
  printf("<%s> Disconnect: %s\n",
    to_string().c_str(), reason.to_string().c_str());
  close();
}

void Connection::on_error(Connection_ptr, TCPException err) {
  printf("<%s> TCP Error: %s\n",
    to_string().c_str(), err.what());
}

void Connection::on_packet_dropped(Packet_ptr, std::string reason) {
  printf("<%s> Packet dropped: %s\n",
    to_string().c_str(), reason.c_str());
}

void Connection::close() {
  request_ = nullptr;
  response_ = nullptr;
  conn_->close();
  server_.close(idx_);
}

Connection::~Connection() {
  //printf("<%s> Deleted\n", to_string().c_str());
}
