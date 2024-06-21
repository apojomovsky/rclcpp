// Copyright 2021 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef RCLCPP__EXPERIMENTAL__SERVICE_INTRA_PROCESS_HPP_
#define RCLCPP__EXPERIMENTAL__SERVICE_INTRA_PROCESS_HPP_

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "rcl/error_handling.h"
#include "rcutils/logging_macros.h"

#include "rclcpp/any_service_callback.hpp"
#include "rclcpp/experimental/buffers/intra_process_buffer.hpp"
#include "rclcpp/experimental/client_intra_process.hpp"
#include "rclcpp/experimental/create_intra_process_buffer.hpp"
#include "rclcpp/experimental/service_intra_process_base.hpp"
#include "rclcpp/logging.hpp"
#include "rclcpp/qos.hpp"
#include "rclcpp/type_support_decl.hpp"

namespace rclcpp
{
namespace experimental
{

template<typename ServiceT>
class ServiceIntraProcess : public ServiceIntraProcessBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS(ServiceIntraProcess)

  using SharedRequest = std::shared_ptr<typename ServiceT::Request>;
  using SharedResponse = std::shared_ptr<typename ServiceT::Response>;

  using Promise = std::promise<SharedResponse>;
  using PromiseWithRequest = std::promise<std::pair<SharedRequest, SharedResponse>>;

  using SharedFuture = std::shared_future<SharedResponse>;
  using SharedFutureWithRequest = std::shared_future<std::pair<SharedRequest, SharedResponse>>;

  using CallbackType = std::function<void (SharedFuture)>;
  using CallbackWithRequestType = std::function<void (SharedFutureWithRequest)>;

  using CallbackTypeValueVariant = std::tuple<CallbackType, SharedFuture, Promise>;
  using CallbackWithRequestTypeValueVariant = std::tuple<
    CallbackWithRequestType, SharedRequest, SharedFutureWithRequest, PromiseWithRequest>;

  using CallbackInfoVariant = std::variant<
    std::promise<SharedResponse>,
    CallbackTypeValueVariant,
    CallbackWithRequestTypeValueVariant>;

  using RequestCallbackPair = std::pair<SharedRequest, CallbackInfoVariant>;
  using ClientIDtoRequest = std::pair<uint64_t, RequestCallbackPair>;

  ServiceIntraProcess(
    std::weak_ptr<rclcpp::Service<ServiceT>> service_handle,
    AnyServiceCallback<ServiceT> callback,
    rclcpp::Context::SharedPtr context,
    const std::string & service_name,
    const rclcpp::QoS & qos_profile)
  : ServiceIntraProcessBase(context, service_name, qos_profile), any_callback_(callback), service_handle_(service_handle)
  {
    // Create the intra-process buffer.
    buffer_ = rclcpp::experimental::create_service_intra_process_buffer<
      ClientIDtoRequest>(qos_profile);
  }

  virtual ~ServiceIntraProcess() = default;

  bool
  is_ready(rcl_wait_set_t * wait_set)
  {
    (void) wait_set;
    return buffer_->has_data();
  }

  void
  store_intra_process_request(
    uint64_t intra_process_client_id,
    RequestCallbackPair request)
  {
    buffer_->add(std::make_pair(intra_process_client_id, std::move(request)));
    gc_.trigger();
    invoke_on_new_request();
  }

  std::shared_ptr<void>
  take_data()
  {
    auto data = std::make_shared<ClientIDtoRequest>(std::move(buffer_->consume()));
    return std::static_pointer_cast<void>(data);
  }

  void send_response(uint64_t intra_process_client_id, SharedResponse & response)
  {
    std::unique_lock<std::recursive_mutex> lock(reentrant_mutex_);

    auto client_it = clients_.find(intra_process_client_id);

    if (client_it == clients_.end()) {
      RCLCPP_WARN(
        rclcpp::get_logger("rclcpp"),
        "Calling intra_process_service_send_response for invalid or no "
        "longer existing client id");

      callback_info_.erase(intra_process_client_id);
      return;
    }

    auto client_intra_process_base = client_it->second.lock();

    if (client_intra_process_base) {
      auto client = std::dynamic_pointer_cast<
        rclcpp::experimental::ClientIntraProcess<ServiceT>>(
        client_intra_process_base);
      CallbackInfoVariant & value = callback_info_[intra_process_client_id];
      client->store_intra_process_response(
        std::make_pair(std::move(response), std::move(value)));
    } else {
      clients_.erase(client_it);
    }

    callback_info_.erase(intra_process_client_id);
  }

  void execute(std::shared_ptr<void> & data)
  {
    auto serv_handle = service_handle_.lock();

    // Return if the service handle is no longer valid
    if (!serv_handle) {
      return;
    }

    auto ptr = std::static_pointer_cast<ClientIDtoRequest>(data);

    uint64_t intra_process_client_id = ptr->first;
    SharedRequest & typed_request = ptr->second.first;
    CallbackInfoVariant & value = ptr->second.second;
    callback_info_.emplace(std::make_pair(intra_process_client_id, std::move(value)));

    // To allow for the user callback to handle deferred responses for IPC in an ambiguous way,
    // we are overloading the rmw_request_id semantics to provide the intra process client ID.
    auto req_id = std::make_shared<rmw_request_id_t>();
    req_id->sequence_number = intra_process_client_id;

    SharedResponse response = any_callback_.dispatch(serv_handle, req_id, std::move(typed_request));

    if (response) {
      send_response(intra_process_client_id, response);
    }
  }

protected:
  using BufferUniquePtr =
    typename rclcpp::experimental::buffers::ServiceIntraProcessBuffer<
    ClientIDtoRequest>::UniquePtr;

  BufferUniquePtr buffer_;

  AnyServiceCallback<ServiceT> any_callback_;

  std::weak_ptr<rclcpp::Service<ServiceT>> service_handle_;

  // Store callback variants in a map to support deferred response
  // access by intra-process client id.
  std::unordered_map<uint64_t, CallbackInfoVariant> callback_info_;
};

}  // namespace experimental
}  // namespace rclcpp

#endif  // RCLCPP__EXPERIMENTAL__SERVICE_INTRA_PROCESS_HPP_
