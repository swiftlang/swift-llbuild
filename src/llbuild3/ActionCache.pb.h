// Generated by the protocol buffer compiler.  DO NOT EDIT!
// NO CHECKED-IN PROTOBUF GENCODE
// source: llbuild3/ActionCache.proto
// Protobuf C++ Version: 5.27.2

#ifndef GOOGLE_PROTOBUF_INCLUDED_llbuild3_2fActionCache_2eproto_2epb_2eh
#define GOOGLE_PROTOBUF_INCLUDED_llbuild3_2fActionCache_2eproto_2epb_2eh

#include <limits>
#include <string>
#include <type_traits>
#include <utility>

#include "google/protobuf/runtime_version.h"
#if PROTOBUF_VERSION != 5027002
#error "Protobuf C++ gencode is built with an incompatible version of"
#error "Protobuf C++ headers/runtime. See"
#error "https://protobuf.dev/support/cross-version-runtime-guarantee/#cpp"
#endif
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/arenastring.h"
#include "google/protobuf/generated_message_tctable_decl.h"
#include "google/protobuf/generated_message_util.h"
#include "google/protobuf/metadata_lite.h"
#include "google/protobuf/generated_message_reflection.h"
#include "google/protobuf/message.h"
#include "google/protobuf/repeated_field.h"  // IWYU pragma: export
#include "google/protobuf/extension_set.h"  // IWYU pragma: export
#include "google/protobuf/generated_enum_reflection.h"
#include "google/protobuf/unknown_field_set.h"
#include "llbuild3/CAS.pb.h"
#include "llbuild3/Common.pb.h"
#include "llbuild3/Label.pb.h"
// @@protoc_insertion_point(includes)

// Must be included last.
#include "google/protobuf/port_def.inc"

#define PROTOBUF_INTERNAL_EXPORT_llbuild3_2fActionCache_2eproto

namespace google {
namespace protobuf {
namespace internal {
class AnyMetadata;
}  // namespace internal
}  // namespace protobuf
}  // namespace google

// Internal implementation detail -- do not use these members.
struct TableStruct_llbuild3_2fActionCache_2eproto {
  static const ::uint32_t offsets[];
};
extern const ::google::protobuf::internal::DescriptorTable
    descriptor_table_llbuild3_2fActionCache_2eproto;
namespace llbuild3 {
class CacheKey;
struct CacheKeyDefaultTypeInternal;
extern CacheKeyDefaultTypeInternal _CacheKey_default_instance_;
class CacheValue;
struct CacheValueDefaultTypeInternal;
extern CacheValueDefaultTypeInternal _CacheValue_default_instance_;
}  // namespace llbuild3
namespace google {
namespace protobuf {
}  // namespace protobuf
}  // namespace google

namespace llbuild3 {
enum CacheKeyType : int {
  CACHE_KEY_TYPE_TASK = 0,
  CACHE_KEY_TYPE_ACTION = 1,
  CacheKeyType_INT_MIN_SENTINEL_DO_NOT_USE_ =
      std::numeric_limits<::int32_t>::min(),
  CacheKeyType_INT_MAX_SENTINEL_DO_NOT_USE_ =
      std::numeric_limits<::int32_t>::max(),
};

bool CacheKeyType_IsValid(int value);
extern const uint32_t CacheKeyType_internal_data_[];
constexpr CacheKeyType CacheKeyType_MIN = static_cast<CacheKeyType>(0);
constexpr CacheKeyType CacheKeyType_MAX = static_cast<CacheKeyType>(1);
constexpr int CacheKeyType_ARRAYSIZE = 1 + 1;
const ::google::protobuf::EnumDescriptor*
CacheKeyType_descriptor();
template <typename T>
const std::string& CacheKeyType_Name(T value) {
  static_assert(std::is_same<T, CacheKeyType>::value ||
                    std::is_integral<T>::value,
                "Incorrect type passed to CacheKeyType_Name().");
  return CacheKeyType_Name(static_cast<CacheKeyType>(value));
}
template <>
inline const std::string& CacheKeyType_Name(CacheKeyType value) {
  return ::google::protobuf::internal::NameOfDenseEnum<CacheKeyType_descriptor,
                                                 0, 1>(
      static_cast<int>(value));
}
inline bool CacheKeyType_Parse(absl::string_view name, CacheKeyType* value) {
  return ::google::protobuf::internal::ParseNamedEnum<CacheKeyType>(
      CacheKeyType_descriptor(), name, value);
}

// ===================================================================


// -------------------------------------------------------------------

class CacheKey final : public ::google::protobuf::Message
/* @@protoc_insertion_point(class_definition:llbuild3.CacheKey) */ {
 public:
  inline CacheKey() : CacheKey(nullptr) {}
  ~CacheKey() override;
  template <typename = void>
  explicit PROTOBUF_CONSTEXPR CacheKey(
      ::google::protobuf::internal::ConstantInitialized);

  inline CacheKey(const CacheKey& from) : CacheKey(nullptr, from) {}
  inline CacheKey(CacheKey&& from) noexcept
      : CacheKey(nullptr, std::move(from)) {}
  inline CacheKey& operator=(const CacheKey& from) {
    CopyFrom(from);
    return *this;
  }
  inline CacheKey& operator=(CacheKey&& from) noexcept {
    if (this == &from) return *this;
    if (GetArena() == from.GetArena()
#ifdef PROTOBUF_FORCE_COPY_IN_MOVE
        && GetArena() != nullptr
#endif  // !PROTOBUF_FORCE_COPY_IN_MOVE
    ) {
      InternalSwap(&from);
    } else {
      CopyFrom(from);
    }
    return *this;
  }

  inline const ::google::protobuf::UnknownFieldSet& unknown_fields() const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return _internal_metadata_.unknown_fields<::google::protobuf::UnknownFieldSet>(::google::protobuf::UnknownFieldSet::default_instance);
  }
  inline ::google::protobuf::UnknownFieldSet* mutable_unknown_fields()
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return _internal_metadata_.mutable_unknown_fields<::google::protobuf::UnknownFieldSet>();
  }

  static const ::google::protobuf::Descriptor* descriptor() {
    return GetDescriptor();
  }
  static const ::google::protobuf::Descriptor* GetDescriptor() {
    return default_instance().GetMetadata().descriptor;
  }
  static const ::google::protobuf::Reflection* GetReflection() {
    return default_instance().GetMetadata().reflection;
  }
  static const CacheKey& default_instance() {
    return *internal_default_instance();
  }
  static inline const CacheKey* internal_default_instance() {
    return reinterpret_cast<const CacheKey*>(
        &_CacheKey_default_instance_);
  }
  static constexpr int kIndexInFileMessages = 0;
  friend void swap(CacheKey& a, CacheKey& b) { a.Swap(&b); }
  inline void Swap(CacheKey* other) {
    if (other == this) return;
#ifdef PROTOBUF_FORCE_COPY_IN_SWAP
    if (GetArena() != nullptr && GetArena() == other->GetArena()) {
#else   // PROTOBUF_FORCE_COPY_IN_SWAP
    if (GetArena() == other->GetArena()) {
#endif  // !PROTOBUF_FORCE_COPY_IN_SWAP
      InternalSwap(other);
    } else {
      ::google::protobuf::internal::GenericSwap(this, other);
    }
  }
  void UnsafeArenaSwap(CacheKey* other) {
    if (other == this) return;
    ABSL_DCHECK(GetArena() == other->GetArena());
    InternalSwap(other);
  }

  // implements Message ----------------------------------------------

  CacheKey* New(::google::protobuf::Arena* arena = nullptr) const final {
    return ::google::protobuf::Message::DefaultConstruct<CacheKey>(arena);
  }
  using ::google::protobuf::Message::CopyFrom;
  void CopyFrom(const CacheKey& from);
  using ::google::protobuf::Message::MergeFrom;
  void MergeFrom(const CacheKey& from) { CacheKey::MergeImpl(*this, from); }

  private:
  static void MergeImpl(
      ::google::protobuf::MessageLite& to_msg,
      const ::google::protobuf::MessageLite& from_msg);

  public:
  bool IsInitialized() const {
    return true;
  }
  ABSL_ATTRIBUTE_REINITIALIZES void Clear() final;
  ::size_t ByteSizeLong() const final;
  ::uint8_t* _InternalSerialize(
      ::uint8_t* target,
      ::google::protobuf::io::EpsCopyOutputStream* stream) const final;
  int GetCachedSize() const { return _impl_._cached_size_.Get(); }

  private:
  void SharedCtor(::google::protobuf::Arena* arena);
  void SharedDtor();
  void InternalSwap(CacheKey* other);
 private:
  friend class ::google::protobuf::internal::AnyMetadata;
  static ::absl::string_view FullMessageName() { return "llbuild3.CacheKey"; }

 protected:
  explicit CacheKey(::google::protobuf::Arena* arena);
  CacheKey(::google::protobuf::Arena* arena, const CacheKey& from);
  CacheKey(::google::protobuf::Arena* arena, CacheKey&& from) noexcept
      : CacheKey(arena) {
    *this = ::std::move(from);
  }
  const ::google::protobuf::Message::ClassData* GetClassData() const final;

 public:
  ::google::protobuf::Metadata GetMetadata() const;
  // nested types ----------------------------------------------------

  // accessors -------------------------------------------------------
  enum : int {
    kLabelFieldNumber = 1,
    kDataFieldNumber = 3,
    kTypeFieldNumber = 2,
  };
  // .llbuild3.Label label = 1;
  bool has_label() const;
  void clear_label() ;
  const ::llbuild3::Label& label() const;
  PROTOBUF_NODISCARD ::llbuild3::Label* release_label();
  ::llbuild3::Label* mutable_label();
  void set_allocated_label(::llbuild3::Label* value);
  void unsafe_arena_set_allocated_label(::llbuild3::Label* value);
  ::llbuild3::Label* unsafe_arena_release_label();

  private:
  const ::llbuild3::Label& _internal_label() const;
  ::llbuild3::Label* _internal_mutable_label();

  public:
  // .llbuild3.CASID data = 3;
  bool has_data() const;
  void clear_data() ;
  const ::llbuild3::CASID& data() const;
  PROTOBUF_NODISCARD ::llbuild3::CASID* release_data();
  ::llbuild3::CASID* mutable_data();
  void set_allocated_data(::llbuild3::CASID* value);
  void unsafe_arena_set_allocated_data(::llbuild3::CASID* value);
  ::llbuild3::CASID* unsafe_arena_release_data();

  private:
  const ::llbuild3::CASID& _internal_data() const;
  ::llbuild3::CASID* _internal_mutable_data();

  public:
  // .llbuild3.CacheKeyType type = 2;
  void clear_type() ;
  ::llbuild3::CacheKeyType type() const;
  void set_type(::llbuild3::CacheKeyType value);

  private:
  ::llbuild3::CacheKeyType _internal_type() const;
  void _internal_set_type(::llbuild3::CacheKeyType value);

  public:
  // @@protoc_insertion_point(class_scope:llbuild3.CacheKey)
 private:
  class _Internal;
  friend class ::google::protobuf::internal::TcParser;
  static const ::google::protobuf::internal::TcParseTable<
      2, 3, 2,
      0, 2>
      _table_;

  static constexpr const void* _raw_default_instance_ =
      &_CacheKey_default_instance_;

  friend class ::google::protobuf::MessageLite;
  friend class ::google::protobuf::Arena;
  template <typename T>
  friend class ::google::protobuf::Arena::InternalHelper;
  using InternalArenaConstructable_ = void;
  using DestructorSkippable_ = void;
  struct Impl_ {
    inline explicit constexpr Impl_(
        ::google::protobuf::internal::ConstantInitialized) noexcept;
    inline explicit Impl_(::google::protobuf::internal::InternalVisibility visibility,
                          ::google::protobuf::Arena* arena);
    inline explicit Impl_(::google::protobuf::internal::InternalVisibility visibility,
                          ::google::protobuf::Arena* arena, const Impl_& from,
                          const CacheKey& from_msg);
    ::google::protobuf::internal::HasBits<1> _has_bits_;
    mutable ::google::protobuf::internal::CachedSize _cached_size_;
    ::llbuild3::Label* label_;
    ::llbuild3::CASID* data_;
    int type_;
    PROTOBUF_TSAN_DECLARE_MEMBER
  };
  union { Impl_ _impl_; };
  friend struct ::TableStruct_llbuild3_2fActionCache_2eproto;
};
// -------------------------------------------------------------------

class CacheValue final : public ::google::protobuf::Message
/* @@protoc_insertion_point(class_definition:llbuild3.CacheValue) */ {
 public:
  inline CacheValue() : CacheValue(nullptr) {}
  ~CacheValue() override;
  template <typename = void>
  explicit PROTOBUF_CONSTEXPR CacheValue(
      ::google::protobuf::internal::ConstantInitialized);

  inline CacheValue(const CacheValue& from) : CacheValue(nullptr, from) {}
  inline CacheValue(CacheValue&& from) noexcept
      : CacheValue(nullptr, std::move(from)) {}
  inline CacheValue& operator=(const CacheValue& from) {
    CopyFrom(from);
    return *this;
  }
  inline CacheValue& operator=(CacheValue&& from) noexcept {
    if (this == &from) return *this;
    if (GetArena() == from.GetArena()
#ifdef PROTOBUF_FORCE_COPY_IN_MOVE
        && GetArena() != nullptr
#endif  // !PROTOBUF_FORCE_COPY_IN_MOVE
    ) {
      InternalSwap(&from);
    } else {
      CopyFrom(from);
    }
    return *this;
  }

  inline const ::google::protobuf::UnknownFieldSet& unknown_fields() const
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return _internal_metadata_.unknown_fields<::google::protobuf::UnknownFieldSet>(::google::protobuf::UnknownFieldSet::default_instance);
  }
  inline ::google::protobuf::UnknownFieldSet* mutable_unknown_fields()
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return _internal_metadata_.mutable_unknown_fields<::google::protobuf::UnknownFieldSet>();
  }

  static const ::google::protobuf::Descriptor* descriptor() {
    return GetDescriptor();
  }
  static const ::google::protobuf::Descriptor* GetDescriptor() {
    return default_instance().GetMetadata().descriptor;
  }
  static const ::google::protobuf::Reflection* GetReflection() {
    return default_instance().GetMetadata().reflection;
  }
  static const CacheValue& default_instance() {
    return *internal_default_instance();
  }
  static inline const CacheValue* internal_default_instance() {
    return reinterpret_cast<const CacheValue*>(
        &_CacheValue_default_instance_);
  }
  static constexpr int kIndexInFileMessages = 1;
  friend void swap(CacheValue& a, CacheValue& b) { a.Swap(&b); }
  inline void Swap(CacheValue* other) {
    if (other == this) return;
#ifdef PROTOBUF_FORCE_COPY_IN_SWAP
    if (GetArena() != nullptr && GetArena() == other->GetArena()) {
#else   // PROTOBUF_FORCE_COPY_IN_SWAP
    if (GetArena() == other->GetArena()) {
#endif  // !PROTOBUF_FORCE_COPY_IN_SWAP
      InternalSwap(other);
    } else {
      ::google::protobuf::internal::GenericSwap(this, other);
    }
  }
  void UnsafeArenaSwap(CacheValue* other) {
    if (other == this) return;
    ABSL_DCHECK(GetArena() == other->GetArena());
    InternalSwap(other);
  }

  // implements Message ----------------------------------------------

  CacheValue* New(::google::protobuf::Arena* arena = nullptr) const final {
    return ::google::protobuf::Message::DefaultConstruct<CacheValue>(arena);
  }
  using ::google::protobuf::Message::CopyFrom;
  void CopyFrom(const CacheValue& from);
  using ::google::protobuf::Message::MergeFrom;
  void MergeFrom(const CacheValue& from) { CacheValue::MergeImpl(*this, from); }

  private:
  static void MergeImpl(
      ::google::protobuf::MessageLite& to_msg,
      const ::google::protobuf::MessageLite& from_msg);

  public:
  bool IsInitialized() const {
    return true;
  }
  ABSL_ATTRIBUTE_REINITIALIZES void Clear() final;
  ::size_t ByteSizeLong() const final;
  ::uint8_t* _InternalSerialize(
      ::uint8_t* target,
      ::google::protobuf::io::EpsCopyOutputStream* stream) const final;
  int GetCachedSize() const { return _impl_._cached_size_.Get(); }

  private:
  void SharedCtor(::google::protobuf::Arena* arena);
  void SharedDtor();
  void InternalSwap(CacheValue* other);
 private:
  friend class ::google::protobuf::internal::AnyMetadata;
  static ::absl::string_view FullMessageName() { return "llbuild3.CacheValue"; }

 protected:
  explicit CacheValue(::google::protobuf::Arena* arena);
  CacheValue(::google::protobuf::Arena* arena, const CacheValue& from);
  CacheValue(::google::protobuf::Arena* arena, CacheValue&& from) noexcept
      : CacheValue(arena) {
    *this = ::std::move(from);
  }
  const ::google::protobuf::Message::ClassData* GetClassData() const final;

 public:
  ::google::protobuf::Metadata GetMetadata() const;
  // nested types ----------------------------------------------------

  // accessors -------------------------------------------------------
  enum : int {
    kStatsFieldNumber = 2,
    kDataFieldNumber = 1,
  };
  // repeated .llbuild3.Stat stats = 2;
  int stats_size() const;
  private:
  int _internal_stats_size() const;

  public:
  void clear_stats() ;
  ::llbuild3::Stat* mutable_stats(int index);
  ::google::protobuf::RepeatedPtrField<::llbuild3::Stat>* mutable_stats();

  private:
  const ::google::protobuf::RepeatedPtrField<::llbuild3::Stat>& _internal_stats() const;
  ::google::protobuf::RepeatedPtrField<::llbuild3::Stat>* _internal_mutable_stats();
  public:
  const ::llbuild3::Stat& stats(int index) const;
  ::llbuild3::Stat* add_stats();
  const ::google::protobuf::RepeatedPtrField<::llbuild3::Stat>& stats() const;
  // .llbuild3.CASID data = 1;
  bool has_data() const;
  void clear_data() ;
  const ::llbuild3::CASID& data() const;
  PROTOBUF_NODISCARD ::llbuild3::CASID* release_data();
  ::llbuild3::CASID* mutable_data();
  void set_allocated_data(::llbuild3::CASID* value);
  void unsafe_arena_set_allocated_data(::llbuild3::CASID* value);
  ::llbuild3::CASID* unsafe_arena_release_data();

  private:
  const ::llbuild3::CASID& _internal_data() const;
  ::llbuild3::CASID* _internal_mutable_data();

  public:
  // @@protoc_insertion_point(class_scope:llbuild3.CacheValue)
 private:
  class _Internal;
  friend class ::google::protobuf::internal::TcParser;
  static const ::google::protobuf::internal::TcParseTable<
      1, 2, 2,
      0, 2>
      _table_;

  static constexpr const void* _raw_default_instance_ =
      &_CacheValue_default_instance_;

  friend class ::google::protobuf::MessageLite;
  friend class ::google::protobuf::Arena;
  template <typename T>
  friend class ::google::protobuf::Arena::InternalHelper;
  using InternalArenaConstructable_ = void;
  using DestructorSkippable_ = void;
  struct Impl_ {
    inline explicit constexpr Impl_(
        ::google::protobuf::internal::ConstantInitialized) noexcept;
    inline explicit Impl_(::google::protobuf::internal::InternalVisibility visibility,
                          ::google::protobuf::Arena* arena);
    inline explicit Impl_(::google::protobuf::internal::InternalVisibility visibility,
                          ::google::protobuf::Arena* arena, const Impl_& from,
                          const CacheValue& from_msg);
    ::google::protobuf::internal::HasBits<1> _has_bits_;
    mutable ::google::protobuf::internal::CachedSize _cached_size_;
    ::google::protobuf::RepeatedPtrField< ::llbuild3::Stat > stats_;
    ::llbuild3::CASID* data_;
    PROTOBUF_TSAN_DECLARE_MEMBER
  };
  union { Impl_ _impl_; };
  friend struct ::TableStruct_llbuild3_2fActionCache_2eproto;
};

// ===================================================================




// ===================================================================


#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif  // __GNUC__
// -------------------------------------------------------------------

// CacheKey

// .llbuild3.Label label = 1;
inline bool CacheKey::has_label() const {
  bool value = (_impl_._has_bits_[0] & 0x00000001u) != 0;
  PROTOBUF_ASSUME(!value || _impl_.label_ != nullptr);
  return value;
}
inline const ::llbuild3::Label& CacheKey::_internal_label() const {
  ::google::protobuf::internal::TSanRead(&_impl_);
  const ::llbuild3::Label* p = _impl_.label_;
  return p != nullptr ? *p : reinterpret_cast<const ::llbuild3::Label&>(::llbuild3::_Label_default_instance_);
}
inline const ::llbuild3::Label& CacheKey::label() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
  // @@protoc_insertion_point(field_get:llbuild3.CacheKey.label)
  return _internal_label();
}
inline void CacheKey::unsafe_arena_set_allocated_label(::llbuild3::Label* value) {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  if (GetArena() == nullptr) {
    delete reinterpret_cast<::google::protobuf::MessageLite*>(_impl_.label_);
  }
  _impl_.label_ = reinterpret_cast<::llbuild3::Label*>(value);
  if (value != nullptr) {
    _impl_._has_bits_[0] |= 0x00000001u;
  } else {
    _impl_._has_bits_[0] &= ~0x00000001u;
  }
  // @@protoc_insertion_point(field_unsafe_arena_set_allocated:llbuild3.CacheKey.label)
}
inline ::llbuild3::Label* CacheKey::release_label() {
  ::google::protobuf::internal::TSanWrite(&_impl_);

  _impl_._has_bits_[0] &= ~0x00000001u;
  ::llbuild3::Label* released = _impl_.label_;
  _impl_.label_ = nullptr;
#ifdef PROTOBUF_FORCE_COPY_IN_RELEASE
  auto* old = reinterpret_cast<::google::protobuf::MessageLite*>(released);
  released = ::google::protobuf::internal::DuplicateIfNonNull(released);
  if (GetArena() == nullptr) {
    delete old;
  }
#else   // PROTOBUF_FORCE_COPY_IN_RELEASE
  if (GetArena() != nullptr) {
    released = ::google::protobuf::internal::DuplicateIfNonNull(released);
  }
#endif  // !PROTOBUF_FORCE_COPY_IN_RELEASE
  return released;
}
inline ::llbuild3::Label* CacheKey::unsafe_arena_release_label() {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  // @@protoc_insertion_point(field_release:llbuild3.CacheKey.label)

  _impl_._has_bits_[0] &= ~0x00000001u;
  ::llbuild3::Label* temp = _impl_.label_;
  _impl_.label_ = nullptr;
  return temp;
}
inline ::llbuild3::Label* CacheKey::_internal_mutable_label() {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  if (_impl_.label_ == nullptr) {
    auto* p = ::google::protobuf::Message::DefaultConstruct<::llbuild3::Label>(GetArena());
    _impl_.label_ = reinterpret_cast<::llbuild3::Label*>(p);
  }
  return _impl_.label_;
}
inline ::llbuild3::Label* CacheKey::mutable_label() ABSL_ATTRIBUTE_LIFETIME_BOUND {
  _impl_._has_bits_[0] |= 0x00000001u;
  ::llbuild3::Label* _msg = _internal_mutable_label();
  // @@protoc_insertion_point(field_mutable:llbuild3.CacheKey.label)
  return _msg;
}
inline void CacheKey::set_allocated_label(::llbuild3::Label* value) {
  ::google::protobuf::Arena* message_arena = GetArena();
  ::google::protobuf::internal::TSanWrite(&_impl_);
  if (message_arena == nullptr) {
    delete reinterpret_cast<::google::protobuf::MessageLite*>(_impl_.label_);
  }

  if (value != nullptr) {
    ::google::protobuf::Arena* submessage_arena = reinterpret_cast<::google::protobuf::MessageLite*>(value)->GetArena();
    if (message_arena != submessage_arena) {
      value = ::google::protobuf::internal::GetOwnedMessage(message_arena, value, submessage_arena);
    }
    _impl_._has_bits_[0] |= 0x00000001u;
  } else {
    _impl_._has_bits_[0] &= ~0x00000001u;
  }

  _impl_.label_ = reinterpret_cast<::llbuild3::Label*>(value);
  // @@protoc_insertion_point(field_set_allocated:llbuild3.CacheKey.label)
}

// .llbuild3.CacheKeyType type = 2;
inline void CacheKey::clear_type() {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  _impl_.type_ = 0;
}
inline ::llbuild3::CacheKeyType CacheKey::type() const {
  // @@protoc_insertion_point(field_get:llbuild3.CacheKey.type)
  return _internal_type();
}
inline void CacheKey::set_type(::llbuild3::CacheKeyType value) {
  _internal_set_type(value);
  // @@protoc_insertion_point(field_set:llbuild3.CacheKey.type)
}
inline ::llbuild3::CacheKeyType CacheKey::_internal_type() const {
  ::google::protobuf::internal::TSanRead(&_impl_);
  return static_cast<::llbuild3::CacheKeyType>(_impl_.type_);
}
inline void CacheKey::_internal_set_type(::llbuild3::CacheKeyType value) {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  _impl_.type_ = value;
}

// .llbuild3.CASID data = 3;
inline bool CacheKey::has_data() const {
  bool value = (_impl_._has_bits_[0] & 0x00000002u) != 0;
  PROTOBUF_ASSUME(!value || _impl_.data_ != nullptr);
  return value;
}
inline const ::llbuild3::CASID& CacheKey::_internal_data() const {
  ::google::protobuf::internal::TSanRead(&_impl_);
  const ::llbuild3::CASID* p = _impl_.data_;
  return p != nullptr ? *p : reinterpret_cast<const ::llbuild3::CASID&>(::llbuild3::_CASID_default_instance_);
}
inline const ::llbuild3::CASID& CacheKey::data() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
  // @@protoc_insertion_point(field_get:llbuild3.CacheKey.data)
  return _internal_data();
}
inline void CacheKey::unsafe_arena_set_allocated_data(::llbuild3::CASID* value) {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  if (GetArena() == nullptr) {
    delete reinterpret_cast<::google::protobuf::MessageLite*>(_impl_.data_);
  }
  _impl_.data_ = reinterpret_cast<::llbuild3::CASID*>(value);
  if (value != nullptr) {
    _impl_._has_bits_[0] |= 0x00000002u;
  } else {
    _impl_._has_bits_[0] &= ~0x00000002u;
  }
  // @@protoc_insertion_point(field_unsafe_arena_set_allocated:llbuild3.CacheKey.data)
}
inline ::llbuild3::CASID* CacheKey::release_data() {
  ::google::protobuf::internal::TSanWrite(&_impl_);

  _impl_._has_bits_[0] &= ~0x00000002u;
  ::llbuild3::CASID* released = _impl_.data_;
  _impl_.data_ = nullptr;
#ifdef PROTOBUF_FORCE_COPY_IN_RELEASE
  auto* old = reinterpret_cast<::google::protobuf::MessageLite*>(released);
  released = ::google::protobuf::internal::DuplicateIfNonNull(released);
  if (GetArena() == nullptr) {
    delete old;
  }
#else   // PROTOBUF_FORCE_COPY_IN_RELEASE
  if (GetArena() != nullptr) {
    released = ::google::protobuf::internal::DuplicateIfNonNull(released);
  }
#endif  // !PROTOBUF_FORCE_COPY_IN_RELEASE
  return released;
}
inline ::llbuild3::CASID* CacheKey::unsafe_arena_release_data() {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  // @@protoc_insertion_point(field_release:llbuild3.CacheKey.data)

  _impl_._has_bits_[0] &= ~0x00000002u;
  ::llbuild3::CASID* temp = _impl_.data_;
  _impl_.data_ = nullptr;
  return temp;
}
inline ::llbuild3::CASID* CacheKey::_internal_mutable_data() {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  if (_impl_.data_ == nullptr) {
    auto* p = ::google::protobuf::Message::DefaultConstruct<::llbuild3::CASID>(GetArena());
    _impl_.data_ = reinterpret_cast<::llbuild3::CASID*>(p);
  }
  return _impl_.data_;
}
inline ::llbuild3::CASID* CacheKey::mutable_data() ABSL_ATTRIBUTE_LIFETIME_BOUND {
  _impl_._has_bits_[0] |= 0x00000002u;
  ::llbuild3::CASID* _msg = _internal_mutable_data();
  // @@protoc_insertion_point(field_mutable:llbuild3.CacheKey.data)
  return _msg;
}
inline void CacheKey::set_allocated_data(::llbuild3::CASID* value) {
  ::google::protobuf::Arena* message_arena = GetArena();
  ::google::protobuf::internal::TSanWrite(&_impl_);
  if (message_arena == nullptr) {
    delete reinterpret_cast<::google::protobuf::MessageLite*>(_impl_.data_);
  }

  if (value != nullptr) {
    ::google::protobuf::Arena* submessage_arena = reinterpret_cast<::google::protobuf::MessageLite*>(value)->GetArena();
    if (message_arena != submessage_arena) {
      value = ::google::protobuf::internal::GetOwnedMessage(message_arena, value, submessage_arena);
    }
    _impl_._has_bits_[0] |= 0x00000002u;
  } else {
    _impl_._has_bits_[0] &= ~0x00000002u;
  }

  _impl_.data_ = reinterpret_cast<::llbuild3::CASID*>(value);
  // @@protoc_insertion_point(field_set_allocated:llbuild3.CacheKey.data)
}

// -------------------------------------------------------------------

// CacheValue

// .llbuild3.CASID data = 1;
inline bool CacheValue::has_data() const {
  bool value = (_impl_._has_bits_[0] & 0x00000001u) != 0;
  PROTOBUF_ASSUME(!value || _impl_.data_ != nullptr);
  return value;
}
inline const ::llbuild3::CASID& CacheValue::_internal_data() const {
  ::google::protobuf::internal::TSanRead(&_impl_);
  const ::llbuild3::CASID* p = _impl_.data_;
  return p != nullptr ? *p : reinterpret_cast<const ::llbuild3::CASID&>(::llbuild3::_CASID_default_instance_);
}
inline const ::llbuild3::CASID& CacheValue::data() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
  // @@protoc_insertion_point(field_get:llbuild3.CacheValue.data)
  return _internal_data();
}
inline void CacheValue::unsafe_arena_set_allocated_data(::llbuild3::CASID* value) {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  if (GetArena() == nullptr) {
    delete reinterpret_cast<::google::protobuf::MessageLite*>(_impl_.data_);
  }
  _impl_.data_ = reinterpret_cast<::llbuild3::CASID*>(value);
  if (value != nullptr) {
    _impl_._has_bits_[0] |= 0x00000001u;
  } else {
    _impl_._has_bits_[0] &= ~0x00000001u;
  }
  // @@protoc_insertion_point(field_unsafe_arena_set_allocated:llbuild3.CacheValue.data)
}
inline ::llbuild3::CASID* CacheValue::release_data() {
  ::google::protobuf::internal::TSanWrite(&_impl_);

  _impl_._has_bits_[0] &= ~0x00000001u;
  ::llbuild3::CASID* released = _impl_.data_;
  _impl_.data_ = nullptr;
#ifdef PROTOBUF_FORCE_COPY_IN_RELEASE
  auto* old = reinterpret_cast<::google::protobuf::MessageLite*>(released);
  released = ::google::protobuf::internal::DuplicateIfNonNull(released);
  if (GetArena() == nullptr) {
    delete old;
  }
#else   // PROTOBUF_FORCE_COPY_IN_RELEASE
  if (GetArena() != nullptr) {
    released = ::google::protobuf::internal::DuplicateIfNonNull(released);
  }
#endif  // !PROTOBUF_FORCE_COPY_IN_RELEASE
  return released;
}
inline ::llbuild3::CASID* CacheValue::unsafe_arena_release_data() {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  // @@protoc_insertion_point(field_release:llbuild3.CacheValue.data)

  _impl_._has_bits_[0] &= ~0x00000001u;
  ::llbuild3::CASID* temp = _impl_.data_;
  _impl_.data_ = nullptr;
  return temp;
}
inline ::llbuild3::CASID* CacheValue::_internal_mutable_data() {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  if (_impl_.data_ == nullptr) {
    auto* p = ::google::protobuf::Message::DefaultConstruct<::llbuild3::CASID>(GetArena());
    _impl_.data_ = reinterpret_cast<::llbuild3::CASID*>(p);
  }
  return _impl_.data_;
}
inline ::llbuild3::CASID* CacheValue::mutable_data() ABSL_ATTRIBUTE_LIFETIME_BOUND {
  _impl_._has_bits_[0] |= 0x00000001u;
  ::llbuild3::CASID* _msg = _internal_mutable_data();
  // @@protoc_insertion_point(field_mutable:llbuild3.CacheValue.data)
  return _msg;
}
inline void CacheValue::set_allocated_data(::llbuild3::CASID* value) {
  ::google::protobuf::Arena* message_arena = GetArena();
  ::google::protobuf::internal::TSanWrite(&_impl_);
  if (message_arena == nullptr) {
    delete reinterpret_cast<::google::protobuf::MessageLite*>(_impl_.data_);
  }

  if (value != nullptr) {
    ::google::protobuf::Arena* submessage_arena = reinterpret_cast<::google::protobuf::MessageLite*>(value)->GetArena();
    if (message_arena != submessage_arena) {
      value = ::google::protobuf::internal::GetOwnedMessage(message_arena, value, submessage_arena);
    }
    _impl_._has_bits_[0] |= 0x00000001u;
  } else {
    _impl_._has_bits_[0] &= ~0x00000001u;
  }

  _impl_.data_ = reinterpret_cast<::llbuild3::CASID*>(value);
  // @@protoc_insertion_point(field_set_allocated:llbuild3.CacheValue.data)
}

// repeated .llbuild3.Stat stats = 2;
inline int CacheValue::_internal_stats_size() const {
  return _internal_stats().size();
}
inline int CacheValue::stats_size() const {
  return _internal_stats_size();
}
inline ::llbuild3::Stat* CacheValue::mutable_stats(int index)
    ABSL_ATTRIBUTE_LIFETIME_BOUND {
  // @@protoc_insertion_point(field_mutable:llbuild3.CacheValue.stats)
  return _internal_mutable_stats()->Mutable(index);
}
inline ::google::protobuf::RepeatedPtrField<::llbuild3::Stat>* CacheValue::mutable_stats()
    ABSL_ATTRIBUTE_LIFETIME_BOUND {
  // @@protoc_insertion_point(field_mutable_list:llbuild3.CacheValue.stats)
  ::google::protobuf::internal::TSanWrite(&_impl_);
  return _internal_mutable_stats();
}
inline const ::llbuild3::Stat& CacheValue::stats(int index) const
    ABSL_ATTRIBUTE_LIFETIME_BOUND {
  // @@protoc_insertion_point(field_get:llbuild3.CacheValue.stats)
  return _internal_stats().Get(index);
}
inline ::llbuild3::Stat* CacheValue::add_stats() ABSL_ATTRIBUTE_LIFETIME_BOUND {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  ::llbuild3::Stat* _add = _internal_mutable_stats()->Add();
  // @@protoc_insertion_point(field_add:llbuild3.CacheValue.stats)
  return _add;
}
inline const ::google::protobuf::RepeatedPtrField<::llbuild3::Stat>& CacheValue::stats() const
    ABSL_ATTRIBUTE_LIFETIME_BOUND {
  // @@protoc_insertion_point(field_list:llbuild3.CacheValue.stats)
  return _internal_stats();
}
inline const ::google::protobuf::RepeatedPtrField<::llbuild3::Stat>&
CacheValue::_internal_stats() const {
  ::google::protobuf::internal::TSanRead(&_impl_);
  return _impl_.stats_;
}
inline ::google::protobuf::RepeatedPtrField<::llbuild3::Stat>*
CacheValue::_internal_mutable_stats() {
  ::google::protobuf::internal::TSanRead(&_impl_);
  return &_impl_.stats_;
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif  // __GNUC__

// @@protoc_insertion_point(namespace_scope)
}  // namespace llbuild3


namespace google {
namespace protobuf {

template <>
struct is_proto_enum<::llbuild3::CacheKeyType> : std::true_type {};
template <>
inline const EnumDescriptor* GetEnumDescriptor<::llbuild3::CacheKeyType>() {
  return ::llbuild3::CacheKeyType_descriptor();
}

}  // namespace protobuf
}  // namespace google

// @@protoc_insertion_point(global_scope)

#include "google/protobuf/port_undef.inc"

#endif  // GOOGLE_PROTOBUF_INCLUDED_llbuild3_2fActionCache_2eproto_2epb_2eh
