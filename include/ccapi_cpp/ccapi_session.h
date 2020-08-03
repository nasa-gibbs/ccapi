#ifndef INCLUDE_CCAPI_CPP_CCAPI_SESSION_H_
#define INCLUDE_CCAPI_CPP_CCAPI_SESSION_H_
#include "ccapi_cpp/ccapi_enable_exchange.h"
#ifdef ENABLE_COINBASE
#include "ccapi_cpp/ccapi_websocket_client_coinbase.h"
#endif
#ifdef ENABLE_GEMINI
#include "ccapi_cpp/ccapi_websocket_client_gemini.h"
#endif
#ifdef ENABLE_KRAKEN
#include "ccapi_cpp/ccapi_websocket_client_kraken.h"
#endif
#ifdef ENABLE_BITSTAMP
#include "ccapi_cpp/ccapi_websocket_client_bitstamp.h"
#endif
#ifdef ENABLE_BITFINEX
#include "ccapi_cpp/ccapi_websocket_client_bitfinex.h"
#endif
#ifdef ENABLE_BITMEX
#include "ccapi_cpp/ccapi_websocket_client_bitmex.h"
#endif
#include "ccapi_cpp/ccapi_session_options.h"
#include "ccapi_cpp/ccapi_session_configs.h"
#include "ccapi_cpp/ccapi_subscription_list.h"
#include <string>
#include <utility>
#include <vector>
#include <map>
#include <unordered_set>
#include "ccapi_cpp/ccapi_event_queue.h"
#include "ccapi_cpp/ccapi_exchange.h"
#include "ccapi_cpp/ccapi_event_dispatcher.h"
#include "ccapi_cpp/ccapi_event_handler.h"
#include "ccapi_cpp/ccapi_event.h"
namespace ccapi {
class Session final {
 public:
  Session(const SessionOptions& options = SessionOptions(), const SessionConfigs& configs = SessionConfigs(),
          EventHandler* eventHandler = 0, EventDispatcher* eventDispatcher = 0)
      : sessionOptions(options),
        sessionConfigs(configs),
        eventHandler(eventHandler),
        eventDispatcher(eventDispatcher) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    if (this->eventHandler) {
      if (!this->eventDispatcher) {
        this->eventDispatcher = &this->defaultEventDispatcher;
      }
    } else {
      if (this->eventDispatcher) {
        throw std::runtime_error("undefined behavior");
      }
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
//  bool openService(std::string serviceName = "") {
//    CCAPI_LOGGER_FUNCTION_ENTER;
//    if (serviceName.empty()) {
//      this->serviceName = this->sessionOptions.defaultSubscriptionService;
//    }
//    if (this->serviceName != this->sessionOptions.defaultSubscriptionService) {
//      CCAPI_LOGGER_FATAL("unsupported service: " + this->serviceName);
//    }
//    CCAPI_LOGGER_FUNCTION_EXIT;
//    return true;
//  }
  void subscribe(const SubscriptionList& subscriptionList) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    std::unordered_set<CorrelationId, CorrelationIdHash> correlationIdSet;
    std::unordered_set<CorrelationId, CorrelationIdHash> duplicateCorrelationIdSet;
    std::unordered_set<std::string> unsupportedExchangePairSet;
    std::unordered_set<std::string> unsupportedExchangeFieldSet;
    std::map<std::string, SubscriptionList> subscriptionListByExchangeMap;
    std::unordered_set<std::string> unsupportedExchangeMarketDepthSet;
    auto exchangePairMap = this->sessionConfigs.getExchangePairMap();
    CCAPI_LOGGER_DEBUG("exchangePairMap = "+toString(exchangePairMap));
    auto exchangeFieldMap = this->sessionConfigs.getExchangeFieldMap();
    CCAPI_LOGGER_DEBUG("exchangeFieldMap = "+toString(exchangeFieldMap));
    for (auto & subscription : subscriptionList.getSubscriptionList()) {
      auto correlationId = subscription.getCorrelationId();
      if (correlationIdSet.find(correlationId) != correlationIdSet.end()) {
        duplicateCorrelationIdSet.insert(correlationId);
      } else {
        correlationIdSet.insert(correlationId);
      }
      auto exchange = subscription.getExchange();
      CCAPI_LOGGER_DEBUG("exchange = "+exchange);
      auto pair = subscription.getPair();
      CCAPI_LOGGER_DEBUG("pair = "+pair);
      auto fieldSet = subscription.getFieldSet();
      auto optionMap = subscription.getOptionMap();
      if (exchangePairMap.find(exchange) == exchangePairMap.end()
          || std::find(exchangePairMap.find(exchange)->second.begin(), exchangePairMap.find(exchange)->second.end(),
                       pair) == exchangePairMap.find(exchange)->second.end()) {
        unsupportedExchangePairSet.insert(exchange + "|" + pair);
      }
      for (auto & field : fieldSet) {
        CCAPI_LOGGER_DEBUG("field = "+field);
        if (exchangeFieldMap.find(exchange) == exchangeFieldMap.end()
            || std::find(exchangeFieldMap.find(exchange)->second.begin(), exchangeFieldMap.find(exchange)->second.end(),
                         field) == exchangeFieldMap.find(exchange)->second.end()) {
          unsupportedExchangeFieldSet.insert(exchange + "|" + field);
        }
        if (field == CCAPI_EXCHANGE_NAME_MARKET_DEPTH) {
          auto depth = std::stoi(optionMap.at(CCAPI_EXCHANGE_NAME_MARKET_DEPTH_MAX));
          if ((exchange == CCAPI_EXCHANGE_NAME_KRAKEN
              && depth > this->sessionConfigs.getWebsocketAvailableMarketDepth().at(exchange).back())
              || (exchange == CCAPI_EXCHANGE_NAME_BITSTAMP
                  && depth > this->sessionConfigs.getWebsocketMaxAvailableMarketDepth().at(exchange))
              || (exchange == CCAPI_EXCHANGE_NAME_BITFINEX
                  && depth > this->sessionConfigs.getWebsocketAvailableMarketDepth().at(exchange).back())) {
            unsupportedExchangeMarketDepthSet.insert(exchange + "|" + toString(depth));
          }
        }
      }
      subscriptionListByExchangeMap[exchange].add(subscription);
    }
    if (!duplicateCorrelationIdSet.empty()) {
      CCAPI_LOGGER_FATAL("duplicated correlation ids: " + toString(duplicateCorrelationIdSet));
    }
    if (!unsupportedExchangePairSet.empty()) {
      CCAPI_LOGGER_FATAL("unsupported exchange pairs: " + toString(unsupportedExchangePairSet));
    }
    if (!unsupportedExchangeFieldSet.empty()) {
      CCAPI_LOGGER_FATAL("unsupported exchange fields: " + toString(unsupportedExchangeFieldSet));
    }
    if (!unsupportedExchangeMarketDepthSet.empty()) {
      CCAPI_LOGGER_FATAL(
          "unsupported exchange market depth: " + toString(unsupportedExchangeMarketDepthSet)
              + ", exceeded max market depth available");
    }
    CCAPI_LOGGER_TRACE("subscriptionListByExchangeMap = "+toString(subscriptionListByExchangeMap));
    if (this->eventDispatcher) {
      this->eventDispatcher->start();
    }
    std::vector<std::thread> sessionWsThreads;
    for (auto & subscriptionListByExchange : subscriptionListByExchangeMap) {
      auto exchange = subscriptionListByExchange.first;
      auto subscriptionList = subscriptionListByExchange.second;
      CCAPI_LOGGER_DEBUG("exchange = "+exchange);
      CCAPI_LOGGER_DEBUG("subscriptionList = "+toString(subscriptionList));
      std::function<void(Event& event)> wsEventHandler = std::bind(&Session::onEvent, this, std::placeholders::_1);
      auto sessionOptions = this->sessionOptions;
      auto sessionConfigs = this->sessionConfigs;
      sessionWsThreads.push_back(
          std::thread([=]() {
            // for unknown reasons, here making websocket clients living on the heap results in 2x faster!!!
              bool found = false;
#ifdef ENABLE_COINBASE
              if (exchange == CCAPI_EXCHANGE_NAME_COINBASE) {
                WebsocketClientCoinbase* ws = new WebsocketClientCoinbase(subscriptionList, wsEventHandler, sessionOptions, sessionConfigs);
                found = true;
                CCAPI_LOGGER_DEBUG("about to connect "+ws->getBaseUrl());
                ws->connect();
                delete ws;
              }
#endif
#ifdef ENABLE_GEMINI
              if (exchange == CCAPI_EXCHANGE_NAME_GEMINI) {
                WebsocketClientGemini* ws = new WebsocketClientGemini(subscriptionList, wsEventHandler, sessionOptions, sessionConfigs);
                found = true;
                CCAPI_LOGGER_DEBUG("about to connect "+ws->getBaseUrl());
                ws->connect();
                delete ws;
              }
#endif
#ifdef ENABLE_KRAKEN
              if (exchange == CCAPI_EXCHANGE_NAME_KRAKEN) {
                WebsocketClientKraken* ws = new WebsocketClientKraken(subscriptionList, wsEventHandler, sessionOptions, sessionConfigs);
                found = true;
                CCAPI_LOGGER_DEBUG("about to connect "+ws->getBaseUrl());
                ws->connect();
                delete ws;
              }
#endif
#ifdef ENABLE_BITSTAMP
              if (exchange == CCAPI_EXCHANGE_NAME_BITSTAMP) {
                WebsocketClientBitstamp* ws = new WebsocketClientBitstamp(subscriptionList, wsEventHandler, sessionOptions, sessionConfigs);
                found = true;
                CCAPI_LOGGER_DEBUG("about to connect "+ws->getBaseUrl());
                ws->connect();
                delete ws;
              }
#endif
#ifdef ENABLE_BITFINEX
              if (exchange == CCAPI_EXCHANGE_NAME_BITFINEX) {
                WebsocketClientBitfinex* ws = new WebsocketClientBitfinex(subscriptionList, wsEventHandler, sessionOptions, sessionConfigs);
                found = true;
                CCAPI_LOGGER_DEBUG("about to connect "+ws->getBaseUrl());
                ws->connect();
                delete ws;
              }
#endif
#ifdef ENABLE_BITMEX
              if (exchange == CCAPI_EXCHANGE_NAME_BITMEX) {
                WebsocketClientBitmex* ws = new WebsocketClientBitmex(subscriptionList, wsEventHandler, sessionOptions, sessionConfigs);
                found = true;
                CCAPI_LOGGER_DEBUG("about to connect "+ws->getBaseUrl());
                ws->connect();
                delete ws;
              }
#endif
              if (!found) {
                CCAPI_LOGGER_FATAL("unsupported exchange: "+exchange);
              }
              //    CCAPI_LOGGER_DEBUG("about to connect "+ws->getBaseUrl());
              //    ws->connect();
              //    delete ws;
            }));
    }
    for (auto& sessionWsThread : sessionWsThreads) {
      sessionWsThread.join();
      CCAPI_LOGGER_TRACE("this thread has joined");
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  void onEvent(Event& event) {
    CCAPI_LOGGER_FUNCTION_ENTER;
    CCAPI_LOGGER_TRACE("event = "+toString(event));
    if (this->eventHandler) {
      CCAPI_LOGGER_TRACE("handle event asynchronously");
      this->eventDispatcher->dispatch([&, event] {
        bool shouldContinue = true;
        try {
          shouldContinue = this->eventHandler->processEvent(event, this);
        } catch(const std::runtime_error& e) {
          CCAPI_LOGGER_ERROR(e.what());
        }
        if (!shouldContinue) {
          CCAPI_LOGGER_DEBUG("about to pause the event dispatcher");
          this->eventDispatcher->pause();
        }
      });
    } else {
      CCAPI_LOGGER_TRACE("handle event synchronously");
      this->eventQueue.push(std::move(event));
    }
    CCAPI_LOGGER_FUNCTION_EXIT;
  }
  EventQueue eventQueue;

 private:
  std::string serviceName;
  SessionOptions sessionOptions;
  SessionConfigs sessionConfigs;
  EventHandler* eventHandler;
  EventDispatcher* eventDispatcher;
  EventDispatcher defaultEventDispatcher;
};
} /* namespace ccapi */
#endif  // INCLUDE_CCAPI_CPP_CCAPI_SESSION_H_