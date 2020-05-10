#include <otree/ObservableTree.h>

#include <forward_list>
#include <iostream>
#include <json11/json11.hpp>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

using namespace otree;

struct Json11Trait {
  using JsonType = json11::Json;
  template <typename T, typename = void> struct Impl;

  static JsonType get(const JsonType &j, const Path &kp) {
    JsonType jret = j;
    for (const auto &key : kp.keys()) {
      if (jret = jret[key]; jret.is_null()) {
        break;
      }
    }
    return jret;
  }

  static void set(JsonType &j, const Path &kp, const JsonType &jvalue) {
    //    throw std::exception{"Method is not supported!"};
  }

  static JsonType get(const JsonType &j, const Path::KeyType &key) {
    return j[key];
  }

  static bool equal(const JsonType &j1, const JsonType &j2) { return j1 == j2; }

  static bool empty(const JsonType &j) { return j.is_null(); }

  template <typename T, typename = bool>
  static T get(const JsonType &j, const Path &kp) {
    return Impl<T>::get(Json11Trait::get(j, kp));
  }

  template <class RawType, class Type>
  using IsTypeOf = std::enable_if_t<
      std::is_same_v<RawType, std::remove_cv_t<std::remove_reference_t<Type>>>,
      void>;

  template <> struct Impl<int> {
    static int get(const JsonType &j) { return j.int_value(); }
  };
  template <> struct Impl<double> {
    static double get(const JsonType &j) { return j.number_value(); }
  };
  template <class String> struct Impl<String, IsTypeOf<std::string, String>> {
    static String get(const JsonType &j) { return j.string_value(); }
  };

  template <> struct Impl<bool> {
    static bool get(const JsonType &j) { return j.bool_value(); }
  };

  template <class Array> struct Impl<Array, IsTypeOf<JsonType::array, Array>> {
    static JsonType::array get(const JsonType &j) { return j.array_items(); }
  };

  template <class JsonObject>
  struct Impl<JsonObject, IsTypeOf<JsonType::object, JsonObject>> {
    static JsonObject get(const JsonType &j) { return j.object_items(); }
  };
};

struct Silencer {
  Silencer() = default;
  Silencer(Silencer &&) = default;
  Silencer &operator=(Silencer &&) = default;
  Silencer(const Silencer &) = delete;
  Silencer &operator=(const Silencer &) = delete;

  template <class T> Silencer &operator<<(const T &t) {
    oss_ << t;
    return *this;
  }
  ~Silencer() { std::cout << oss_.str() << "\n"; }

  std::ostringstream oss_;
};

Silencer getSilencer() {
  Silencer s;
  return s;
}

#define outstream getSilencer()

using MyTree = ObservableTree<json11::Json, Json11Trait, std::mutex>;

void functionDump(json11::Json jold, json11::Json jnew) {
  static int i = 0;
  outstream << "functiondump:\n" << ++i << ".old = " << jold.dump() << "\n"
            << i << ".new = " << jnew.dump();
}

int main() {
  auto config = new MyTree;

  std::string strFirst =
      R"({"config":{"usb":{"enabled":0,"sanitized":1},"customcheck":{"enabled":1,"options":[1,2,3]}}})";
  std::string strSecond =
      R"({"config":{"usb":{"enabled":0,"sanitized":1},"customcheck":{"enabled":1,"options":[1,2,3]},"hello":1,"world":"nguyen van con","nguyen":["n","g"],"van":"van","con":0.001,"dai":{"ca":{"con":"number one!"}}}})";

  auto jconfig1 = json11::Json::parse(strFirst, strFirst);
  auto jconfig2 = json11::Json::parse(strSecond, strSecond);

  auto dump = [](json11::Json jold, json11::Json jnew) {
    static int i = 0;
    outstream << ++i << ".old = " << jold.dump() << "\n"
              << i << ".new = " << jnew.dump();
  };

  {
    config->modificationSignal("config/usb/enabled")->connect(dump);
    config->modificationSignal("/config/media_security")->connect(dump);
    config->modificationSignal("/config/media_security/blocked")->connect(dump);
    config->modificationSignal("config/customcheck/enabled")->connect(dump);
    config->modificationSignal("config/customcheck/options")->connect(dump);
    config->modificationSignal("config")->connect(functionDump);
    config->modificationSignal("config/hello")->connect(functionDump);
    config->modificationSignal("config/world")->connect(functionDump);
    config->modificationSignal("config/nguyen")->connect(functionDump);
    config->modificationSignal("config/van")->connect(functionDump);
    config->modificationSignal("config/con")->connect(functionDump);
    auto daicon = config->modificationSignal("config/dai")->connect(functionDump);
    auto cacon = config->modificationSignal("config/dai/ca")->connect(functionDump);
    auto concon =
        config->modificationSignal("config/dai/ca/con")->connect(functionDump);

    outstream << "New batch ----------------------";
    config->set(jconfig1);

    outstream << "New batch ----------------------";
    // With json11, we don't have ability of modifying a json, then just
    // Test for notification when value is set only, don't care about value is
    // set or not
    config->set("config/usb/enabled", 1);
    config->set("/config/media_security/blocked", 1);
    config->set("/config/media_security", "hello world");

    config->set("config/usb/enabled", 2);
    config->set("/config/media_security/blocked", 3);
    config->set("/config/media_security", "hello world nguyen van con");
    config->set("config", {});

    config->set("config", jconfig2);
    outstream << "New batch ----------------------";
    config->set(jconfig2);

    outstream << "New batch ----------------------";
    outstream << "disconnect several connection";
    daicon.disconnect();
    cacon.disconnect();
    concon.disconnect();

    config->set(jconfig1);

    outstream << "New batch ----------------------";

    config->set(jconfig2);

    //    for (int i = 0; i < 1; ++i) {
    //      outstream << "New batch ----------------------";
    //      config->set(first);
    //      outstream << "New batch ----------------------";
    //      config->set(second);
    //    }
  }

  delete config;
  return 0;
}
