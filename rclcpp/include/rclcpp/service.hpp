// Copyright 2014 Open Source Robotics Foundation, Inc.
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

#ifndef RCLCPP__SERVICE_HPP_
#define RCLCPP__SERVICE_HPP_

#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include "rcl/error_handling.h"
#include "rcl/event_callback.h"
#include "rcl/service.h"
#include "rcl/service_introspection.h"

#include "rmw/error_handling.h"
#include "rmw/impl/cpp/demangle.hpp"
#include "rmw/rmw.h"

#include "tracetools/tracetools.h"

#include "rclcpp/any_service_callback.hpp"
#include "rclcpp/clock.hpp"
#include "rclcpp/detail/cpp_callback_trampoline.hpp"
#include "rclcpp/detail/resolve_use_intra_process.hpp"
#include "rclcpp/exceptions.hpp"
#include "rclcpp/expand_topic_or_service_name.hpp"
#include "rclcpp/experimental/intra_process_manager.hpp"
#include "rclcpp/experimental/service_intra_process.hpp"
#include "rclcpp/intra_process_setting.hpp"
#include "rclcpp/logging.hpp"
#include "rclcpp/macros.hpp"
#include "rclcpp/qos.hpp"
#include "rclcpp/type_support_decl.hpp"
#include "rclcpp/visibility_control.hpp"

namespace rclcpp
{

namespace node_interfaces
{
class NodeBaseInterface;
}  // namespace node_interfaces

class ServiceBase
{
public:
  RCLCPP_SMART_PTR_DEFINITIONS_NOT_COPYABLE(ServiceBase)

  RCLCPP_PUBLIC
  explicit ServiceBase(
    std::shared_ptr<rclcpp::node_interfaces::NodeBaseInterface> node_base);

  RCLCPP_PUBLIC
  virtual ~ServiceBase() = default;

  /// Return the name of the service.
  /** \return The name of the service. */
  RCLCPP_PUBLIC
  const char *
  get_service_name();

  /// Return the rcl_service_t service handle in a std::shared_ptr.
  /**
   * This handle remains valid after the Service is destroyed.
   * The actual rcl service is not finalized until it is out of scope everywhere.
   */
  RCLCPP_PUBLIC
  std::shared_ptr<rcl_service_t>
  get_service_handle();

  /// Return the rcl_service_t service handle in a std::shared_ptr.
  /**
   * This handle remains valid after the Service is destroyed.
   * The actual rcl service is not finalized until it is out of scope everywhere.
   */
  RCLCPP_PUBLIC
  std::shared_ptr<const rcl_service_t>
  get_service_handle() const;

  /// Take the next request from the service as a type erased pointer.
  /**
   * This type erased version of \sa Service::take_request() is useful when
   * using the service in a type agnostic way with methods like
   * ServiceBase::create_request(), ServiceBase::create_request_header(), and
   * ServiceBase::handle_request().
   *
   * \param[out] request_out The type erased pointer to a service request object
   *   into which the middleware will copy the taken request.
   * \param[out] request_id_out The output id for the request which can be used
   *   to associate response with this request in the future.
   * \returns true if the request was taken, otherwise false.
   * \throws rclcpp::exceptions::RCLError based exceptions if the underlying
   *   rcl calls fail.
   */
  RCLCPP_PUBLIC
  bool
  take_type_erased_request(void * request_out, rmw_request_id_t & request_id_out);

  virtual
  std::shared_ptr<void>
  create_request() = 0;

  virtual
  std::shared_ptr<rmw_request_id_t>
  create_request_header() = 0;

  virtual
  void
  handle_request(
    std::shared_ptr<rmw_request_id_t> request_header,
    std::shared_ptr<void> request) = 0;

  /// Exchange the "in use by wait set" state for this service.
  /**
   * This is used to ensure this service is not used by multiple
   * wait sets at the same time.
   *
   * \param[in] in_use_state the new state to exchange into the state, true
   *   indicates it is now in use by a wait set, and false is that it is no
   *   longer in use by a wait set.
   * \returns the previous state.
   */
  RCLCPP_PUBLIC
  bool
  exchange_in_use_by_wait_set_state(bool in_use_state);

  /// Get the actual response publisher QoS settings, after the defaults have been determined.
  /**
   * The actual configuration applied when using RMW_QOS_POLICY_*_SYSTEM_DEFAULT
   * can only be resolved after the creation of the service, and it
   * depends on the underlying rmw implementation.
   * If the underlying setting in use can't be represented in ROS terms,
   * it will be set to RMW_QOS_POLICY_*_UNKNOWN.
   * May throw runtime_error when an unexpected error occurs.
   *
   * \return The actual response publisher qos settings.
   * \throws std::runtime_error if failed to get qos settings
   */
  RCLCPP_PUBLIC
  rclcpp::QoS
  get_response_publisher_actual_qos() const;

  /// Get the actual request subscription QoS settings, after the defaults have been determined.
  /**
   * The actual configuration applied when using RMW_QOS_POLICY_*_SYSTEM_DEFAULT
   * can only be resolved after the creation of the service, and it
   * depends on the underlying rmw implementation.
   * If the underlying setting in use can't be represented in ROS terms,
   * it will be set to RMW_QOS_POLICY_*_UNKNOWN.
   * May throw runtime_error when an unexpected error occurs.
   *
   * \return The actual request subscription qos settings.
   * \throws std::runtime_error if failed to get qos settings
   */
  RCLCPP_PUBLIC
  rclcpp::QoS
  get_request_subscription_actual_qos() const;

  /// Return the waitable for intra-process
  /**
   * \return the waitable sharedpointer for intra-process, or nullptr if intra-process is not setup.
   * \throws std::runtime_error if the intra process manager is destroyed
   */
  RCLCPP_PUBLIC
  rclcpp::Waitable::SharedPtr
  get_intra_process_waitable();

  /// Set a callback to be called when each new request is received.
  /**
   * The callback receives a size_t which is the number of requests received
   * since the last time this callback was called.
   * Normally this is 1, but can be > 1 if requests were received before any
   * callback was set.
   *
   * Since this callback is called from the middleware, you should aim to make
   * it fast and not blocking.
   * If you need to do a lot of work or wait for some other event, you should
   * spin it off to another thread, otherwise you risk blocking the middleware.
   *
   * Calling it again will clear any previously set callback.
   *
   *
   * An exception will be thrown if the callback is not callable.
   *
   * This function is thread-safe.
   *
   * If you want more information available in the callback, like the service
   * or other information, you may use a lambda with captures or std::bind.
   *
   * \sa rmw_service_set_on_new_request_callback
   * \sa rcl_service_set_on_new_request_callback
   *
   * \param[in] callback functor to be called when a new request is received
   */
  void
  set_on_new_request_callback(std::function<void(size_t)> callback)
  {
    if (!callback) {
      throw std::invalid_argument(
              "The callback passed to set_on_new_request_callback "
              "is not callable.");
    }

    auto new_callback =
      [callback, this](size_t number_of_requests) {
        try {
          callback(number_of_requests);
        } catch (const std::exception & exception) {
          RCLCPP_ERROR_STREAM(
            node_logger_,
            "rclcpp::ServiceBase@" << this <<
              " caught " << rmw::impl::cpp::demangle(exception) <<
              " exception in user-provided callback for the 'on new request' callback: " <<
              exception.what());
        } catch (...) {
          RCLCPP_ERROR_STREAM(
            node_logger_,
            "rclcpp::ServiceBase@" << this <<
              " caught unhandled exception in user-provided callback " <<
              "for the 'on new request' callback");
        }
      };

    std::lock_guard<std::recursive_mutex> lock(callback_mutex_);

    // Set it temporarily to the new callback, while we replace the old one.
    // This two-step setting, prevents a gap where the old std::function has
    // been replaced but the middleware hasn't been told about the new one yet.
    set_on_new_request_callback(
      rclcpp::detail::cpp_callback_trampoline<decltype(new_callback), const void *, size_t>,
      static_cast<const void *>(&new_callback));

    // Store the std::function to keep it in scope, also overwrites the existing one.
    on_new_request_callback_ = new_callback;

    // Set it again, now using the permanent storage.
    set_on_new_request_callback(
      rclcpp::detail::cpp_callback_trampoline<
        decltype(on_new_request_callback_), const void *, size_t>,
      static_cast<const void *>(&on_new_request_callback_));
  }

  /// Unset the callback registered for new requests, if any.
  void
  clear_on_new_request_callback()
  {
    std::lock_guard<std::recursive_mutex> lock(callback_mutex_);
    if (on_new_request_callback_) {
      set_on_new_request_callback(nullptr, nullptr);
      on_new_request_callback_ = nullptr;
    }
  }

protected:
  RCLCPP_DISABLE_COPY(ServiceBase)

  RCLCPP_PUBLIC
  rcl_node_t *
  get_rcl_node_handle();

  RCLCPP_PUBLIC
  const rcl_node_t *
  get_rcl_node_handle() const;

  RCLCPP_PUBLIC
  void
  set_on_new_request_callback(rcl_event_callback_t callback, const void * user_data);

  using IntraProcessManagerWeakPtr =
    std::weak_ptr<rclcpp::experimental::IntraProcessManager>;

  /// Implementation detail.
  RCLCPP_PUBLIC
  void
  setup_intra_process(
    uint64_t intra_process_service_id,
    IntraProcessManagerWeakPtr weak_ipm);

  std::shared_ptr<rclcpp::experimental::ServiceIntraProcessBase> service_intra_process_;

  std::shared_ptr<rcl_node_t> node_handle_;
  std::shared_ptr<rclcpp::Context> context_;

  std::recursive_mutex callback_mutex_;
  // It is important to declare on_new_request_callback_ before
  // service_handle_, so on destruction the service is
  // destroyed first. Otherwise, the rmw service callback
  // would point briefly to a destroyed function.
  std::function<void(size_t)> on_new_request_callback_{nullptr};
  // Declare service_handle_ after callback
  std::shared_ptr<rcl_service_t> service_handle_;
  bool owns_rcl_handle_ = true;

  rclcpp::Logger node_logger_;

  std::atomic<bool> in_use_by_wait_set_{false};

  std::recursive_mutex ipc_mutex_;
  bool use_intra_process_{false};
  IntraProcessManagerWeakPtr weak_ipm_;
  uint64_t intra_process_service_id_;
};

template<typename ServiceT>
class Service
  : public ServiceBase,
  public std::enable_shared_from_this<Service<ServiceT>>
{
public:
  using CallbackType = std::function<
    void (
      const std::shared_ptr<typename ServiceT::Request>,
      std::shared_ptr<typename ServiceT::Response>)>;

  using CallbackWithHeaderType = std::function<
    void (
      const std::shared_ptr<rmw_request_id_t>,
      const std::shared_ptr<typename ServiceT::Request>,
      std::shared_ptr<typename ServiceT::Response>)>;
  RCLCPP_SMART_PTR_DEFINITIONS(Service)

  /// Default constructor.
  /**
   * The constructor for a Service is almost never called directly.
   * Instead, services should be instantiated through the function
   * rclcpp::create_service().
   *
   * \param[in] node_base NodeBaseInterface pointer that is used in part of the setup.
   * \param[in] service_name Name of the topic to publish to.
   * \param[in] any_callback User defined callback to call when a client request is received.
   * \param[in] service_options options for the subscription.
   * \param[in] ipc_setting Intra-process communication setting for the service.
   */
  Service(
    std::shared_ptr<rclcpp::node_interfaces::NodeBaseInterface> node_base,
    const std::string & service_name,
    AnyServiceCallback<ServiceT> any_callback,
    rcl_service_options_t & service_options,
    rclcpp::IntraProcessSetting ipc_setting = rclcpp::IntraProcessSetting::NodeDefault)
  : ServiceBase(node_base), any_callback_(any_callback),
    srv_type_support_handle_(rosidl_typesupport_cpp::get_service_type_support_handle<ServiceT>())
  {
    // rcl does the static memory allocation here
    service_handle_ = std::shared_ptr<rcl_service_t>(
      new rcl_service_t, [handle = node_handle_, service_name](rcl_service_t * service)
      {
        if (rcl_service_fini(service, handle.get()) != RCL_RET_OK) {
          RCLCPP_ERROR(
            rclcpp::get_node_logger(handle.get()).get_child("rclcpp"),
            "Error in destruction of rcl service handle: %s",
            rcl_get_error_string().str);
          rcl_reset_error();
        }
        delete service;
      });
    *service_handle_.get() = rcl_get_zero_initialized_service();

    rcl_ret_t ret = rcl_service_init(
      service_handle_.get(),
      node_handle_.get(),
      srv_type_support_handle_,
      service_name.c_str(),
      &service_options);
    if (ret != RCL_RET_OK) {
      if (ret == RCL_RET_SERVICE_NAME_INVALID) {
        auto rcl_node_handle = get_rcl_node_handle();
        // this will throw on any validation problem
        rcl_reset_error();
        expand_topic_or_service_name(
          service_name,
          rcl_node_get_name(rcl_node_handle),
          rcl_node_get_namespace(rcl_node_handle),
          true);
      }

      rclcpp::exceptions::throw_from_rcl_error(ret, "could not create service");
    }
    TRACEPOINT(
      rclcpp_service_callback_added,
      static_cast<const void *>(get_service_handle().get()),
      static_cast<const void *>(&any_callback_));
#ifndef TRACETOOLS_DISABLED
    any_callback_.register_callback_for_tracing();
#endif

    // Setup intra process if requested.
    if (rclcpp::detail::resolve_use_intra_process(ipc_setting, *node_base)) {
      create_intra_process_service();
    }
  }

  /// Default constructor.
  /**
   * The constructor for a Service is almost never called directly.
   * Instead, services should be instantiated through the function
   * rclcpp::create_service().
   *
   * \param[in] node_base NodeBaseInterface pointer that is used in part of the setup.
   * \param[in] service_handle service handle.
   * \param[in] any_callback User defined callback to call when a client request is received.
   * \param[in] ipc_setting Intra-process communication setting for the service.
   */
  Service(
    std::shared_ptr<rclcpp::node_interfaces::NodeBaseInterface> node_base,
    std::shared_ptr<rcl_service_t> service_handle,
    AnyServiceCallback<ServiceT> any_callback,
    rclcpp::IntraProcessSetting ipc_setting = rclcpp::IntraProcessSetting::NodeDefault)
  : ServiceBase(node_base), any_callback_(any_callback),
    srv_type_support_handle_(rosidl_typesupport_cpp::get_service_type_support_handle<ServiceT>())
  {
    // check if service handle was initialized
    if (!rcl_service_is_valid(service_handle.get())) {
      // *INDENT-OFF* (prevent uncrustify from making unnecessary indents here)
      throw std::runtime_error(
        std::string("rcl_service_t in constructor argument must be initialized beforehand."));
      // *INDENT-ON*
    }

    service_handle_ = service_handle;
    TRACEPOINT(
      rclcpp_service_callback_added,
      static_cast<const void *>(get_service_handle().get()),
      static_cast<const void *>(&any_callback_));
#ifndef TRACETOOLS_DISABLED
    any_callback_.register_callback_for_tracing();
#endif

    // Setup intra process if requested.
    if (rclcpp::detail::resolve_use_intra_process(ipc_setting, *node_base)) {
      create_intra_process_service();
    }
  }

  /// Default constructor.
  /**
   * The constructor for a Service is almost never called directly.
   * Instead, services should be instantiated through the function
   * rclcpp::create_service().
   *
   * \param[in] node_base NodeBaseInterface pointer that is used in part of the setup.
   * \param[in] service_handle service handle.
   * \param[in] any_callback User defined callback to call when a client request is received.
   * \param[in] ipc_setting Intra-process communication setting for the service.
   */
  Service(
    std::shared_ptr<rclcpp::node_interfaces::NodeBaseInterface> node_base,
    rcl_service_t * service_handle,
    AnyServiceCallback<ServiceT> any_callback,
    rclcpp::IntraProcessSetting ipc_setting = rclcpp::IntraProcessSetting::NodeDefault)
  : ServiceBase(node_base), any_callback_(any_callback),
    srv_type_support_handle_(rosidl_typesupport_cpp::get_service_type_support_handle<ServiceT>())
  {
    // check if service handle was initialized
    if (!rcl_service_is_valid(service_handle)) {
      // *INDENT-OFF* (prevent uncrustify from making unnecessary indents here)
      throw std::runtime_error(
        std::string("rcl_service_t in constructor argument must be initialized beforehand."));
      // *INDENT-ON*
    }

    // In this case, rcl owns the service handle memory
    service_handle_ = std::shared_ptr<rcl_service_t>(new rcl_service_t);
    service_handle_->impl = service_handle->impl;
    TRACEPOINT(
      rclcpp_service_callback_added,
      static_cast<const void *>(get_service_handle().get()),
      static_cast<const void *>(&any_callback_));
#ifndef TRACETOOLS_DISABLED
    any_callback_.register_callback_for_tracing();
#endif
    // Setup intra process if requested.
    if (rclcpp::detail::resolve_use_intra_process(ipc_setting, *node_base)) {
      create_intra_process_service();
    }
  }

  Service() = delete;

  virtual ~Service()
  {
    if (!use_intra_process_) {
      return;
    }
    auto ipm = weak_ipm_.lock();
    if (!ipm) {
      // TODO(ivanpauno): should this raise an error?
      RCLCPP_WARN(
        rclcpp::get_logger("rclcpp"),
        "Intra process manager died before than a service.");
      return;
    }
    ipm->remove_service(intra_process_service_id_);
  }

  /// Take the next request from the service.
  /**
   * \sa ServiceBase::take_type_erased_request().
   *
   * \param[out] request_out The reference to a service request object
   *   into which the middleware will copy the taken request.
   * \param[out] request_id_out The output id for the request which can be used
   *   to associate response with this request in the future.
   * \returns true if the request was taken, otherwise false.
   * \throws rclcpp::exceptions::RCLError based exceptions if the underlying
   *   rcl calls fail.
   */
  bool
  take_request(typename ServiceT::Request & request_out, rmw_request_id_t & request_id_out)
  {
    return this->take_type_erased_request(&request_out, request_id_out);
  }

  std::shared_ptr<void>
  create_request() override
  {
    return std::make_shared<typename ServiceT::Request>();
  }

  std::shared_ptr<rmw_request_id_t>
  create_request_header() override
  {
    return std::make_shared<rmw_request_id_t>();
  }

  void
  handle_request(
    std::shared_ptr<rmw_request_id_t> request_header,
    std::shared_ptr<void> request) override
  {
    auto typed_request = std::static_pointer_cast<typename ServiceT::Request>(request);
    auto response = any_callback_.dispatch(this->shared_from_this(), request_header, typed_request);
    if (response) {
      send_response(*request_header, *response);
    }
  }

  void
  send_response(rmw_request_id_t & req_id, typename ServiceT::Response & response)
  {
    rcl_ret_t ret = rcl_send_response(get_service_handle().get(), &req_id, &response);

    if (ret == RCL_RET_TIMEOUT) {
      RCLCPP_WARN(
        node_logger_.get_child("rclcpp"),
        "failed to send response to %s (timeout): %s",
        this->get_service_name(), rcl_get_error_string().str);
      rcl_reset_error();
      return;
    }
    if (ret != RCL_RET_OK) {
      rclcpp::exceptions::throw_from_rcl_error(ret, "failed to send response");
    }
  }

  void
  create_intra_process_service()
  {
    // Check if the QoS is compatible with intra-process.
    auto qos_profile = get_request_subscription_actual_qos();

    if (qos_profile.history() != rclcpp::HistoryPolicy::KeepLast) {
      throw std::invalid_argument(
              "intraprocess communication allowed only with keep last history qos policy");
    }
    if (qos_profile.depth() == 0) {
      throw std::invalid_argument(
              "intraprocess communication is not allowed with 0 depth qos policy");
    }
    if (qos_profile.durability() != rclcpp::DurabilityPolicy::Volatile) {
      throw std::invalid_argument(
              "intraprocess communication allowed only with volatile durability");
    }

    // Create a ServiceIntraProcess which will be given to the intra-process manager.
    using ServiceIntraProcessT = rclcpp::experimental::ServiceIntraProcess<ServiceT>;

    service_intra_process_ = std::make_shared<ServiceIntraProcessT>(
      any_callback_,
      context_,
      this->get_service_name(),
      qos_profile);

    using rclcpp::experimental::IntraProcessManager;
    auto ipm = context_->get_sub_context<IntraProcessManager>();
    uint64_t intra_process_service_id = ipm->add_intra_process_service(service_intra_process_);
    this->setup_intra_process(intra_process_service_id, ipm);
  }

  /// Configure service introspection.
  /**
   * \param[in] clock clock to use to generate introspection timestamps
   * \param[in] qos_service_event_pub QoS settings to use when creating the introspection publisher
   * \param[in] introspection_state the state to set introspection to
   */
  void
  configure_introspection(
    Clock::SharedPtr clock, const QoS & qos_service_event_pub,
    rcl_service_introspection_state_t introspection_state)
  {
    rcl_publisher_options_t pub_opts = rcl_publisher_get_default_options();
    pub_opts.qos = qos_service_event_pub.get_rmw_qos_profile();

    rcl_ret_t ret = rcl_service_configure_service_introspection(
      service_handle_.get(),
      node_handle_.get(),
      clock->get_clock_handle(),
      srv_type_support_handle_,
      pub_opts,
      introspection_state);

    if (RCL_RET_OK != ret) {
      rclcpp::exceptions::throw_from_rcl_error(ret, "failed to configure service introspection");
    }
  }

private:
  RCLCPP_DISABLE_COPY(Service)

  AnyServiceCallback<ServiceT> any_callback_;

  const rosidl_service_type_support_t * srv_type_support_handle_;
};

}  // namespace rclcpp

#endif  // RCLCPP__SERVICE_HPP_
