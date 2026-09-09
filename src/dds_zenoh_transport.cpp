#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "internal/dds_transport.hpp"

#ifdef DDS_ABSTRACT_HAS_ZENOH
#include <zenoh.hxx>
#endif

namespace dds_abstract {

#ifdef DDS_ABSTRACT_HAS_ZENOH
namespace {

DdsBytes FromZenoh(const zenoh::Bytes& payload) {
  auto source = payload.as_vector();
  DdsBytes result(source.size());
  for (std::size_t i = 0; i < source.size(); ++i)
    result[i] = static_cast<std::byte>(source[i]);
  return result;
}

zenoh::Bytes ToZenoh(DdsByteView payload) {
  std::vector<std::uint8_t> result(payload.size());
  for (std::size_t i = 0; i < payload.size(); ++i)
    result[i] = static_cast<std::uint8_t>(payload[i]);
  return zenoh::Bytes(std::move(result));
}

struct ServerReply {
  std::mutex mutex;
  std::condition_variable changed;
  std::optional<DdsBytes> value;
  bool cancelled{};
};

struct ClientReply {
  std::mutex mutex;
  std::condition_variable changed;
  std::optional<DdsBytes> value;
  bool done{};
  std::string error;
};

class DdsZenohTransport final : public DdsTransport {
 public:
  void Open(const DdsNodeAddress* broadcast,
            const DdsNodeAddress* request_address, bool is_server) override {
    if (is_open_.exchange(true))
      throw std::logic_error("Zenoh transport is already open");
    ResetEntities();
    is_server_ = is_server;
    broadcast_key_ = broadcast == nullptr ? "" : broadcast->resource;
    request_key_ = request_address == nullptr ? "" : request_address->resource;
    try {
      session_ = std::make_unique<zenoh::Session>(
          zenoh::Session::open(zenoh::Config::create_default()));
      if (is_server_) {
        if (broadcast != nullptr) {
          publisher_ = std::make_unique<zenoh::Publisher>(
              session_->declare_publisher(zenoh::KeyExpr(broadcast_key_)));
        }
        if (request_address != nullptr) {
          auto on_query = [this](const zenoh::Query& query) {
            auto state = std::make_shared<ServerReply>();
            const auto id = std::to_string(++next_id_);
            {
              std::lock_guard lock(pending_mutex_);
              if (!is_open_) return;
              pending_[id] = state;
            }
            DdsBytes payload;
            if (auto value = query.get_payload())
              payload = FromZenoh(value->get());
            Enqueue(
                {DdsIncomingMessage::Kind::kRequest, std::move(payload), id});

            std::unique_lock lock(state->mutex);
            state->changed.wait(lock, [&] {
              return state->value.has_value() || state->cancelled;
            });
            if (state->value)
              query.reply(zenoh::KeyExpr(request_key_), ToZenoh(*state->value));
            {
              std::lock_guard pending_lock(pending_mutex_);
              pending_.erase(id);
            }
          };
          queryable_ = std::make_unique<zenoh::Queryable<void>>(
              session_->declare_queryable(zenoh::KeyExpr(request_key_),
                                          std::move(on_query),
                                          zenoh::closures::none));
        }
      } else {
        if (broadcast != nullptr) {
          auto on_sample = [this](const zenoh::Sample& sample) {
            Enqueue({DdsIncomingMessage::Kind::kBroadcast,
                     FromZenoh(sample.get_payload()),
                     {}});
          };
          subscriber_ = std::make_unique<zenoh::Subscriber<void>>(
              session_->declare_subscriber(zenoh::KeyExpr(broadcast_key_),
                                           std::move(on_sample),
                                           zenoh::closures::none));
        }
      }
    } catch (...) {
      is_open_ = false;
      ResetEntities();
      throw;
    }
  }

  void Close() noexcept override {
    if (!is_open_.exchange(false)) return;
    {
      std::lock_guard lock(queue_mutex_);
      queue_changed_.notify_all();
    }
    std::lock_guard lock(pending_mutex_);
    for (auto& [id, state] : pending_) {
      std::lock_guard state_lock(state->mutex);
      state->cancelled = true;
      state->changed.notify_all();
    }
  }

  void Publish(DdsByteView payload) override {
    if (!is_server_ || !is_open_ || !publisher_)
      throw std::logic_error("Zenoh publisher is not open");
    publisher_->put(ToZenoh(payload));
  }

  DdsBytes Request(DdsByteView payload,
                   std::chrono::milliseconds timeout) override {
    if (is_server_ || !is_open_ || request_key_.empty())
      throw std::logic_error("Zenoh requester is not open");
    auto state = std::make_shared<ClientReply>();
    auto on_reply = [state](const zenoh::Reply& reply) {
      std::lock_guard lock(state->mutex);
      if (state->done) return;
      if (reply.is_ok())
        state->value = FromZenoh(reply.get_ok().get_payload());
      else
        state->error = reply.get_err().get_payload().as_string();
      state->done = true;
      state->changed.notify_all();
    };
    auto on_done = [state] {
      std::lock_guard lock(state->mutex);
      state->done = true;
      state->changed.notify_all();
    };
    zenoh::Session::GetOptions options;
    options.payload = ToZenoh(payload);
    options.timeout_ms = static_cast<std::uint64_t>(timeout.count());
    session_->get(zenoh::KeyExpr(request_key_), "", std::move(on_reply),
                  std::move(on_done), std::move(options));

    std::unique_lock lock(state->mutex);
    if (!state->changed.wait_for(lock, timeout, [&] { return state->done; }))
      throw std::runtime_error("Zenoh request timed out");
    if (!state->error.empty())
      throw std::runtime_error("Zenoh reply error: " + state->error);
    if (!state->value)
      throw std::runtime_error("Zenoh query completed without a reply");
    return std::move(*state->value);
  }

  bool TryReceive(const std::atomic_bool& stop_requested,
                  std::chrono::milliseconds timeout,
                  DdsIncomingMessage* message) override {
    if (message == nullptr) throw std::invalid_argument("message is null");
    std::unique_lock lock(queue_mutex_);
    queue_changed_.wait_for(lock, timeout, [&] {
      return stop_requested.load() || !queue_.empty() || !is_open_;
    });
    if (queue_.empty()) return false;
    *message = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }

  void Reply(const DdsIncomingMessage& incoming, DdsByteView payload) override {
    std::shared_ptr<ServerReply> state;
    {
      std::lock_guard lock(pending_mutex_);
      auto found = pending_.find(incoming.correlation_id);
      if (found == pending_.end()) return;
      state = found->second;
    }
    std::lock_guard lock(state->mutex);
    state->value = DdsBytes(payload.begin(), payload.end());
    state->changed.notify_all();
  }

  ~DdsZenohTransport() override {
    Close();
    ResetEntities();
  }

 private:
  void Enqueue(DdsIncomingMessage value) {
    std::lock_guard lock(queue_mutex_);
    if (!is_open_) return;
    queue_.push_back(std::move(value));
    queue_changed_.notify_one();
  }
  void ResetEntities() noexcept {
    queryable_.reset();
    subscriber_.reset();
    publisher_.reset();
    session_.reset();
  }

  std::atomic_bool is_open_{};
  bool is_server_{};
  std::string broadcast_key_, request_key_;
  std::unique_ptr<zenoh::Session> session_;
  std::unique_ptr<zenoh::Publisher> publisher_;
  std::unique_ptr<zenoh::Subscriber<void>> subscriber_;
  std::unique_ptr<zenoh::Queryable<void>> queryable_;
  std::mutex queue_mutex_;
  std::condition_variable_any queue_changed_;
  std::deque<DdsIncomingMessage> queue_;
  std::mutex pending_mutex_;
  std::unordered_map<std::string, std::shared_ptr<ServerReply>> pending_;
  std::atomic_uint64_t next_id_{};
};

}  // namespace
#endif

std::unique_ptr<DdsTransport> MakeDdsZenohTransport() {
#ifdef DDS_ABSTRACT_HAS_ZENOH
  return std::make_unique<DdsZenohTransport>();
#else
  throw std::runtime_error("Zenoh backend was not enabled at build time");
#endif
}

}  // namespace dds_abstract
