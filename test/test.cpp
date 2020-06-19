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
  template <typename T, typename = void>
  struct Impl;

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

  template <>
  struct Impl<int> {
    static int get(const JsonType &j) { return j.int_value(); }
  };
  template <>
  struct Impl<double> {
    static double get(const JsonType &j) { return j.number_value(); }
  };
  template <class String>
  struct Impl<String, IsTypeOf<std::string, String>> {
    static String get(const JsonType &j) { return j.string_value(); }
  };

  template <>
  struct Impl<bool> {
    static bool get(const JsonType &j) { return j.bool_value(); }
  };

  template <class Array>
  struct Impl<Array, IsTypeOf<JsonType::array, Array>> {
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

  template <class T>
  Silencer &operator<<(const T &t) {
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

using MyTree = ObservableTree<json11::Json, Json11Trait, otree::Path,
                              otree::Path::KeyType, std::mutex>;

void functionDump(json11::Json jold, json11::Json jnew) {
  static int i = 0;
  outstream << "functiondump:\n"
            << ++i << ".old = " << jold.dump() << "\n"
            << i << ".new = " << jnew.dump();
}

#include <boost/property_tree/info_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <sstream>

namespace pt = boost::property_tree;

namespace otree {
class PtreeTrait {
 public:
  using NodeType = pt::ptree;
  using PathType = pt::ptree::path_type;
  using KeyType = pt::ptree::key_type;

  static NodeType get(const NodeType &node, const PathType &path) {
    try {
      return node.get_child(path);
    } catch (...) {
      return {};
    }
  }

  static void set(NodeType &node, const PathType &path, const NodeType &value) {
    node.put_child(path, value);
  }

  static NodeType get(const NodeType &node, const KeyType &key) {
    try {
      return node.get_child(key);
    } catch (...) {
      return {};
    }
  }

  static bool equal(const NodeType &node1, const NodeType &node2) {
    return node1 == node2;
  }

  static bool empty(const NodeType &j) { return j.begin() == j.end(); }
};

class PPathIter {
  using PathImpl = otree::Path;
  using BaseIt = otree::Path::iterator;
  std::shared_ptr<PathImpl> p;
  int index = -1;

 public:
  using ValueType = PathImpl::KeyType;

  PPathIter(const PtreeTrait::PathType &ppath) : p{new PathImpl{ppath.dump()}} {
    if (!p->keys().empty()) {
      index = 0;
    }
  }

  PPathIter() = default;

  bool operator==(const PPathIter &otherIt) const {
    return (p == otherIt.p && index == otherIt.index) ||
           ((otherIt.index == -1) && (index == -1));
  }

  bool operator!=(const PPathIter &other) const { return !(*this == other); }

  PPathIter &operator++() {
    if (p && index != -1) {
      if (static_cast<size_t>(index) < p->keys().size() - 1) {
        ++index;
      } else {
        index = -1;
      }
    }
    return *this;
  }

  PPathIter operator++(int) {
    auto it = *this;
    if (p && index != -1) {
      if (static_cast<size_t>(index) < p->keys().size() - 1) {
        ++index;
      } else {
        index = -1;
      }
    }
    return it;
  }

  const ValueType &operator*() const { return p->keys()[index]; }
};

auto begin(const PtreeTrait::PathType &p) { return PPathIter{p}; }

auto end(const PtreeTrait::PathType &p) { return PPathIter{}; }

}  // namespace otree

using OPtree = otree::ObservableTree<PtreeTrait::NodeType, PtreeTrait,
                                     PtreeTrait::PathType, PtreeTrait::KeyType>;

int main() {
  //    tree.get_optional<int>()

  auto config = new OPtree;

  std::string strFirst =
      R"({"config":{"usb":{"enabled":0,"sanitized":1},"customcheck":{"enabled":1,"options":[1,2,3]}}})";
  std::string strSecond =
      R"({"config":{"usb":{"enabled":1,"sanitized":1},"customcheck":{"enabled":1,"options":[1,2,3]},"hello":1,"world":"nguyen van con","nguyen":["n","g"],"van":"van","con":0.001,"dai":{"ca":{"con":"number one!"}}}})";

  auto iss1 = std::istringstream{strFirst};
  auto iss2 = std::istringstream{strSecond};

  //  auto jconfig1 = json11::Json::parse(strFirst, strFirst);
  //  auto jconfig2 = json11::Json::parse(strSecond, strSecond);
  auto jconfig1 = pt::ptree{};
  auto jconfig2 = pt::ptree{};
  pt::json_parser::read_json(iss1, jconfig1);
  pt::json_parser::read_json(iss2, jconfig2);

  auto ptreedump = [](const OPtree::NodeType &jold,
                      const OPtree::NodeType &jnew) {
    //    std::cout << "old = ";
    //    pt::write_info(std::cout, jold);

    //    std::cout << "new = ";
    //    pt::write_info(std::cout, jnew);
  };

  {
    auto connection1 = config->modificationSignal("config/usb/enabled")
                           ->connect([](const OPtree::NodeType &jold,
                                        const OPtree::NodeType &jnew) {
                             //              auto oldV =
                             //              jold.get_value_optional<int>();
                             //              auto newV =
                             //              jnew.get_value_optional<int>();
                             //              std::cout << "old = " << (oldV ?
                             //              oldV.value() : -1) << std::endl;
                             //              std::cout << "new = " << (newV ?
                             //              newV.value() : -1) << std::endl;
                           });

    auto connection2 =
        config->modificationSignal("config/usb/enabled")->connect(ptreedump);
    auto connection3 =
        config->modificationSignal("config/usb/enabled")->connect(ptreedump);
    //    for (int i = 0; i < 2000; ++i)
    //      config->modificationSignal("config/usb/enabled")->connect(ptreedump);

    config->set(jconfig1);

    outstream << "New batch ----------------------";
    //    connection1.disconnect();
    //    connection2.disconnect();
    //    connection3.disconnect();
    for (int i = 0; i < 100000; ++i) {
      config->set(jconfig1);
      config->set(jconfig2);
    }
  }
  //  while (true) {
  //    std::this_thread::sleep_for(std::chrono::seconds{1});
  //  }

  delete config;
  return 0;
}
