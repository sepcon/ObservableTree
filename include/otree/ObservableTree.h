#pragma once

#include <algorithm>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace otree {

struct NoEffectMutex {
  void lock() {}
  void unlock(){};
  void try_lock() {}
};

template <class Mutex> struct LockGuard {
  Mutex &m_;
  LockGuard(Mutex &m) : m_(m) { m_.lock(); }
  ~LockGuard() { m_.unlock(); }
};

class Path {
public:
  using KeyType = std::string;
  using Keys = std::vector<KeyType>;
  using Sep = char;
  using iterator = Keys::iterator;
  using const_iterator = Keys::const_iterator;

  Path() = default;
  Path(const Path &) = default;
  Path(Path &&) = default;
  Path &operator=(const Path &) = default;
  Path &operator=(Path &&) = default;

  Path(Keys &&ks) : keys_{std::move(ks)} {}
  Path(const Keys &ks) : keys_{ks} {}

  template <class String>
  Path(const String &path, Sep sep = '/')
      : keys_{toKeys(path, sep)}, sep_{sep} {}
  const Keys &keys() const { return keys_; }

  std::string toString() const {
    auto str = std::string{};
    for (const auto &key : keys()) {
      str += key + sep_;
    }
    if (!str.empty()) {
      str.resize(str.size() - 1);
    }
    return str;
  }

  Path operator/(const KeyType &key) const {
    auto newPath = Path{keys_};
    newPath.keys_.push_back(key);
    return newPath;
  }

  Path operator/(KeyType &&key) const {
    auto newPath = Path{keys_};
    newPath.keys_.push_back(std::move(key));
    return newPath;
  }

  Path operator/(const Path &path) const {
    auto newKeys = keys_;
    std::copy(std::begin(path.keys()), std::end(path.keys()),
              std::back_inserter(newKeys));
    return Path{std::move(newKeys)};
  }

  template <class String> bool operator==(const String &p) const {
    auto ks = toKeys(p);
    if (ks.size() != keys_.size()) {
      return false;
    } else {
      auto mi = keys_.rbegin();
      auto i = ks.rbegin();
      auto miend = keys_.rend();
      auto iend = ks.rend();
      while (mi != miend && i != iend) {
        if (*mi != *i) {
          return false;
        }
        ++mi;
        ++i;
      }
      return true;
    }
  }

  auto begin() { return keys_.begin(); }
  auto end() { return keys_.end(); }
  auto rbegin() { return keys_.rbegin(); }
  auto rend() { return keys_.rend(); }
  auto begin() const { return keys_.begin(); }
  auto end() const { return keys_.end(); }
  auto rbegin() const { return keys_.rbegin(); }
  auto rend() const { return keys_.rend(); }

private:
  template <class String>
  static Keys toKeys(const String &path, Sep sep = '/') {
    using std::begin;
    using std::end;
    auto keys = Keys{};

    auto istart = begin(path);
    auto iend = end(path);
    auto it = istart;

    while (it != iend) {
      if (*it == sep) {
        if (*istart != sep) {
          keys.emplace_back(istart, it);
        }
        istart = ++it;
      } else {
        ++it;
      }
    }
    if (istart != iend) {
      auto last = KeyType{istart, iend};
      if (last.back() == '\0') {
        last.resize(last.size() - 1);
      }
      if (!last.empty()) {
        keys.emplace_back(std::move(last));
      }
    }
    return keys;
  }

  Keys keys_;
  Sep sep_ = '/';
};

bool operator<(const Path &kp1, const Path &kp2) {
  return kp1.keys() < kp2.keys();
}

bool operator==(const Path &kp1, const Path &kp2) {
  return kp1.keys() == kp2.keys();
}

template <class Tree>
class ModificationSignal
    : public std::enable_shared_from_this<ModificationSignal<Tree>> {
  using NodeType = typename Tree::NodeType;
  using SlotType = std::function<void(const NodeType &, const NodeType &)>;
  using SlotIDType = SlotType *;
  using SlotListType = std::list<std::unique_ptr<SlotType>>;
  using MutexType = typename Tree::MutexType;

  static constexpr SlotIDType SlotIDInvalid = nullptr;

  class Connection {
  public:
    using SignalType = ModificationSignal<Tree>;
    using SignalRefType = std::weak_ptr<SignalType>;
    using SlotIDType = SignalType::SlotIDType;

    void disconnect() {
      if (slotID_) {
        if (auto sig = signalRef_.lock()) {
          sig->disconnect(slotID_);
        }
      }
    }

    bool connected() const { return slotID_ != SlotIDInvalid; }

    Connection(Connection &&other)
        : signalRef_{std::move(other.signalRef_)}, slotID_{other.slotID_} {
      other.slotID_ = SlotIDInvalid;
    }

    Connection &operator=(Connection &&other) {
      signalRef_ = std::move(other.signalRef_);
      slotID_ = other.slotID_;
      other.slotID_ = SlotIDInvalid;
    }

    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

  private:
    friend class ModificationSignal;
    Connection() = default;
    Connection(SignalRefType &&sigref, SlotIDType slotID)
        : signalRef_{std::move(sigref)}, slotID_{slotID} {}

    SignalRefType signalRef_;
    SlotIDType slotID_;
  };

  enum class ImplType : char { Single, Multi };

  class ImplIF {
  public:
    virtual ~ImplIF() = default;
    virtual SlotIDType connect(SlotType &&sl) = 0;
    virtual void disconnect(SlotIDType slID) = 0;
    virtual void onChanged(const NodeType &oldNode,
                           const NodeType &newNode) = 0;
    virtual SlotListType stealSlots() = 0;
    virtual bool connected() const = 0;
    virtual ImplType type() = 0;
  };

  class SingleImpl : public ImplIF {
    std::unique_ptr<SlotType> slot_ = nullptr;

  public:
    ~SingleImpl() = default;
    ImplType type() override { return ImplType::Single; }
    bool connected() const override { return !!slot_; }
    SlotIDType connect(SlotType &&sl) override {
      slot_ = std::make_unique<SlotType>(std::move(sl));
      return slot_.get();
    }

    void disconnect(SlotIDType slotID) override {
      if (slot_.get() == slotID) {
        slot_.reset();
      }
    }

    SlotListType stealSlots() override {
      auto sls = SlotListType{};
      sls.push_back(std::move(slot_));
      return sls;
    }

    void onChanged(const NodeType &oldNode, const NodeType &newNode) override {
      if (slot_ && *slot_) {
        (*slot_)(oldNode, newNode);
      }
    }
  };

  class MultiImpl : public ImplIF {
    SlotListType slots_;

  public:
    MultiImpl() = default;
    MultiImpl(SlotListType &&list) : slots_{std::move(list)} {}
    ImplType type() override { return ImplType::Multi; }
    bool connected() const override { return !slots_.empty(); }
    SlotIDType connect(SlotType &&sl) override {
      if (sl) {
        auto slotID = new SlotType{std::move(sl)};
        slots_.emplace_back(slotID);
        return slotID;
      }
      return SlotIDInvalid;
    }

    void disconnect(SlotIDType slotID) override {
      slots_.erase(
          std::find_if(slots_.begin(), slots_.end(), [slotID](const auto &sl) {
            return sl.get() == slotID;
          }));
    }

    void onChanged(const NodeType &oldNode, const NodeType &newNode) override {
      for (auto &sl : slots_) {
        (*sl)(oldNode, newNode);
      }
    }

    SlotListType stealSlots() override { return std::move(slots_); }
  };

public:
  Connection connect(SlotType sl) {
    SlotIDType slotID = SlotIDInvalid;
    if (sl) {
      LockGuard lock(mutex_);
      if (!impl_) {
        impl_ = std::make_unique<SingleImpl>();
      } else if ((impl_->type() == ImplType::Single) && impl_->connected()) {
        impl_.reset(new MultiImpl{impl_->stealSlots()});
      }
      slotID = impl_->connect(std::move(sl));
    }
    if (slotID != SlotIDInvalid) {
      return Connection{weak_from_this(), slotID};
    }
    return {};
  }

private:
  template <class TreeClass> friend class SignalMgr;

  bool connected() const {
    LockGuard lock(mutex_);
    return impl_ && impl_->connected();
  }

  void disconnect(SlotIDType slotID) {
    LockGuard lock(mutex_);
    if (impl_) {
      impl_->disconnect(slotID);
    }
  }

  void operator()(const NodeType &jOld, const NodeType &jNew) {
    LockGuard lock(mutex_);
    if (impl_) {
      impl_->onChanged(jOld, jNew);
    }
  }

  // TBD: Should be compressed pair for case of NoEffectMutex to reduce memory
  // usage
  std::unique_ptr<ImplIF> impl_;
  mutable MutexType mutex_;
};

template <class Tree> class SignalMgr {
public:
  using KeyType = Path::KeyType;
  using SignalType = ModificationSignal<Tree>;
  using TraitType = typename Tree::TraitType;
  using NodeType = typename Tree::NodeType;
  using SignalPtrType = std::shared_ptr<SignalType>;

  SignalPtrType createSignal(const Path &path) {
    if (!path.keys().empty()) {
      return createSignal(std::begin(path.keys()), std::end(path.keys()));
    }
    return {};
  }

  bool onChanged(const NodeType &oldNode, const NodeType &newNode) {
    bool hasChanged = false;
    if (!TraitType::empty(oldNode) || !TraitType::empty(newNode)) {
      for (auto &[key, subAndObservers] : signalsMap_) {
        auto oldValue = TraitType::get(oldNode, key);
        auto newValue = TraitType::get(newNode, key);
        auto &[modSignal, sub] = subAndObservers;
        if (sub) {
          if (sub->onChanged(oldValue, newValue)) {
            hasChanged = true;
            if (modSignal) {
              (*modSignal)(oldValue, newValue);
            };
            // if sub value changed then current one must be changed
            // don't need to waste time comparing
            continue;
          }
        }

        auto diff = !TraitType::equal(oldValue, newValue);
        hasChanged |= diff;

        if (modSignal) {
          if (modSignal->connected()) {
            if (diff) {
              (*modSignal)(oldValue, newValue);
            }
          } else {
            modSignal.reset();
          }
        }
      }
    }
    return hasChanged;
  }

  bool onChanged(const Path &path, const NodeType &oldData,
                 const NodeType &newData) {
    if (auto signalPtr = getSignal(path)) {
      if (!TraitType::equal(oldData, newData)) {
        (*signalPtr)(oldData, newData);
        return true;
      }
    }
    return false;
  }

private:
  struct MySignalAndChild {
    SignalPtrType signalPtr_;
    std::unique_ptr<SignalMgr> child_;
  };

  template <class Iterator>
  SignalPtrType createSignal(Iterator itFirstKey, Iterator itLastKey) {
    auto &subAndSig = signalsMap_[*itFirstKey];
    if (++itFirstKey != itLastKey) {
      if (!subAndSig.child_) {
        subAndSig.child_ = std::make_unique<SignalMgr>();
      }
      return subAndSig.child_->createSignal(itFirstKey, itLastKey);
    } else {
      if (!subAndSig.signalPtr_) {
        subAndSig.signalPtr_.reset(new SignalType);
      }
      return subAndSig.signalPtr_;
    }
  }

  SignalPtrType getSignal(const Path &path) {
    auto beg = std::begin(path);
    auto end = std::end(path);
    if (beg != end) {
      if (auto sigNChild = getSigNChild(beg, end)) {
        return sigNChild->signalPtr_;
      }
    }
    return {};
  }

  template <class KeyItor>
  MySignalAndChild *getSigNChild(KeyItor pathBegin, KeyItor pathEnd) {
    if (auto it = signalsMap_.find(*pathBegin); it != signalsMap_.end()) {
      auto sigNChild = &(it->second);
      if (++pathBegin != pathEnd) {
        if (sigNChild->child_) {
          return sigNChild->child_->getSigNChild(pathBegin, pathEnd);
        }
      } else {
        return sigNChild;
      }
    }
    return nullptr;
  }

  std::map<KeyType, MySignalAndChild> signalsMap_;
};

template <class Node, class Trait, class Mutex = NoEffectMutex>
class ObservableTree {
  using MyType = ObservableTree<Node, Trait, Mutex>;

public:
  using NodeType = Node;
  using TraitType = Trait;
  using MutexType = Mutex;
  using PathType = Path;
  using SignalPtrType = typename SignalMgr<MyType>::SignalPtrType;

  SignalPtrType modificationSignal(const Path &path) {
    LockGuard lock(mutex_);
    return signalMgr_.createSignal(path);
  }

  void set(NodeType &&newData) {
    LockGuard lock(mutex_);
    signalMgr_.onChanged(root_, newData);
    root_ = std::move(newData);
  }

  void set(const NodeType &newData) {
    LockGuard lock(mutex_);
    signalMgr_.onChanged(root_, newData);
    root_ = newData;
  }

  // tbd: set(data, path)
  void set(const Path &path, const NodeType &newNode) {
    LockGuard lock(mutex_);
    signalMgr_.onChanged(path, TraitType::get(root_, path), newNode);
    TraitType::set(root_, path, newNode);
  }

  void set(const Path &path, NodeType &&newNode) {
    LockGuard lock(mutex_);
    signalMgr_.onChanged(path, TraitType::get(root_, path), newNode);
    TraitType::set(root_, path, std::move(newNode));
  }

  NodeType get() const {
    LockGuard lock(mutex_);
    return root_;
  }

  NodeType get(const Path &path) {
    LockGuard lock(mutex_);
    return Trait::get(path);
  }

  template <typename T> T get(const Path &path) {
    LockGuard lock(mutex_);
    return Trait::template get<T>(root_, path);
  }

private:
  SignalMgr<MyType> signalMgr_;
  NodeType root_;
  mutable Mutex mutex_;
};

} // namespace otree
