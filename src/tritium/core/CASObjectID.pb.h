// Generated by the protocol buffer compiler.  DO NOT EDIT!
// NO CHECKED-IN PROTOBUF GENCODE
// source: tritium/core/CASObjectID.proto
// Protobuf C++ Version: 5.27.2

#ifndef GOOGLE_PROTOBUF_INCLUDED_tritium_2fcore_2fCASObjectID_2eproto_2epb_2eh
#define GOOGLE_PROTOBUF_INCLUDED_tritium_2fcore_2fCASObjectID_2eproto_2epb_2eh

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
#include "google/protobuf/unknown_field_set.h"
// @@protoc_insertion_point(includes)

// Must be included last.
#include "google/protobuf/port_def.inc"

#define PROTOBUF_INTERNAL_EXPORT_tritium_2fcore_2fCASObjectID_2eproto

namespace google {
namespace protobuf {
namespace internal {
class AnyMetadata;
}  // namespace internal
}  // namespace protobuf
}  // namespace google

// Internal implementation detail -- do not use these members.
struct TableStruct_tritium_2fcore_2fCASObjectID_2eproto {
  static const ::uint32_t offsets[];
};
extern const ::google::protobuf::internal::DescriptorTable
    descriptor_table_tritium_2fcore_2fCASObjectID_2eproto;
namespace tritium {
namespace core {
class CASObjectID;
struct CASObjectIDDefaultTypeInternal;
extern CASObjectIDDefaultTypeInternal _CASObjectID_default_instance_;
}  // namespace core
}  // namespace tritium
namespace google {
namespace protobuf {
}  // namespace protobuf
}  // namespace google

namespace tritium {
namespace core {

// ===================================================================


// -------------------------------------------------------------------

class CASObjectID final : public ::google::protobuf::Message
/* @@protoc_insertion_point(class_definition:tritium.core.CASObjectID) */ {
 public:
  inline CASObjectID() : CASObjectID(nullptr) {}
  ~CASObjectID() override;
  template <typename = void>
  explicit PROTOBUF_CONSTEXPR CASObjectID(
      ::google::protobuf::internal::ConstantInitialized);

  inline CASObjectID(const CASObjectID& from) : CASObjectID(nullptr, from) {}
  inline CASObjectID(CASObjectID&& from) noexcept
      : CASObjectID(nullptr, std::move(from)) {}
  inline CASObjectID& operator=(const CASObjectID& from) {
    CopyFrom(from);
    return *this;
  }
  inline CASObjectID& operator=(CASObjectID&& from) noexcept {
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
  static const CASObjectID& default_instance() {
    return *internal_default_instance();
  }
  static inline const CASObjectID* internal_default_instance() {
    return reinterpret_cast<const CASObjectID*>(
        &_CASObjectID_default_instance_);
  }
  static constexpr int kIndexInFileMessages = 0;
  friend void swap(CASObjectID& a, CASObjectID& b) { a.Swap(&b); }
  inline void Swap(CASObjectID* other) {
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
  void UnsafeArenaSwap(CASObjectID* other) {
    if (other == this) return;
    ABSL_DCHECK(GetArena() == other->GetArena());
    InternalSwap(other);
  }

  // implements Message ----------------------------------------------

  CASObjectID* New(::google::protobuf::Arena* arena = nullptr) const final {
    return ::google::protobuf::Message::DefaultConstruct<CASObjectID>(arena);
  }
  using ::google::protobuf::Message::CopyFrom;
  void CopyFrom(const CASObjectID& from);
  using ::google::protobuf::Message::MergeFrom;
  void MergeFrom(const CASObjectID& from) { CASObjectID::MergeImpl(*this, from); }

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
  void InternalSwap(CASObjectID* other);
 private:
  friend class ::google::protobuf::internal::AnyMetadata;
  static ::absl::string_view FullMessageName() { return "tritium.core.CASObjectID"; }

 protected:
  explicit CASObjectID(::google::protobuf::Arena* arena);
  CASObjectID(::google::protobuf::Arena* arena, const CASObjectID& from);
  CASObjectID(::google::protobuf::Arena* arena, CASObjectID&& from) noexcept
      : CASObjectID(arena) {
    *this = ::std::move(from);
  }
  const ::google::protobuf::Message::ClassData* GetClassData() const final;

 public:
  ::google::protobuf::Metadata GetMetadata() const;
  // nested types ----------------------------------------------------

  // accessors -------------------------------------------------------
  enum : int {
    kBytesFieldNumber = 1,
  };
  // bytes bytes = 1;
  void clear_bytes() ;
  const std::string& bytes() const;
  template <typename Arg_ = const std::string&, typename... Args_>
  void set_bytes(Arg_&& arg, Args_... args);
  std::string* mutable_bytes();
  PROTOBUF_NODISCARD std::string* release_bytes();
  void set_allocated_bytes(std::string* value);

  private:
  const std::string& _internal_bytes() const;
  inline PROTOBUF_ALWAYS_INLINE void _internal_set_bytes(
      const std::string& value);
  std::string* _internal_mutable_bytes();

  public:
  // @@protoc_insertion_point(class_scope:tritium.core.CASObjectID)
 private:
  class _Internal;
  friend class ::google::protobuf::internal::TcParser;
  static const ::google::protobuf::internal::TcParseTable<
      0, 1, 0,
      0, 2>
      _table_;

  static constexpr const void* _raw_default_instance_ =
      &_CASObjectID_default_instance_;

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
                          const CASObjectID& from_msg);
    ::google::protobuf::internal::ArenaStringPtr bytes_;
    mutable ::google::protobuf::internal::CachedSize _cached_size_;
    PROTOBUF_TSAN_DECLARE_MEMBER
  };
  union { Impl_ _impl_; };
  friend struct ::TableStruct_tritium_2fcore_2fCASObjectID_2eproto;
};

// ===================================================================




// ===================================================================


#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif  // __GNUC__
// -------------------------------------------------------------------

// CASObjectID

// bytes bytes = 1;
inline void CASObjectID::clear_bytes() {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  _impl_.bytes_.ClearToEmpty();
}
inline const std::string& CASObjectID::bytes() const
    ABSL_ATTRIBUTE_LIFETIME_BOUND {
  // @@protoc_insertion_point(field_get:tritium.core.CASObjectID.bytes)
  return _internal_bytes();
}
template <typename Arg_, typename... Args_>
inline PROTOBUF_ALWAYS_INLINE void CASObjectID::set_bytes(Arg_&& arg,
                                                     Args_... args) {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  _impl_.bytes_.SetBytes(static_cast<Arg_&&>(arg), args..., GetArena());
  // @@protoc_insertion_point(field_set:tritium.core.CASObjectID.bytes)
}
inline std::string* CASObjectID::mutable_bytes() ABSL_ATTRIBUTE_LIFETIME_BOUND {
  std::string* _s = _internal_mutable_bytes();
  // @@protoc_insertion_point(field_mutable:tritium.core.CASObjectID.bytes)
  return _s;
}
inline const std::string& CASObjectID::_internal_bytes() const {
  ::google::protobuf::internal::TSanRead(&_impl_);
  return _impl_.bytes_.Get();
}
inline void CASObjectID::_internal_set_bytes(const std::string& value) {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  _impl_.bytes_.Set(value, GetArena());
}
inline std::string* CASObjectID::_internal_mutable_bytes() {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  return _impl_.bytes_.Mutable( GetArena());
}
inline std::string* CASObjectID::release_bytes() {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  // @@protoc_insertion_point(field_release:tritium.core.CASObjectID.bytes)
  return _impl_.bytes_.Release();
}
inline void CASObjectID::set_allocated_bytes(std::string* value) {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  _impl_.bytes_.SetAllocated(value, GetArena());
  #ifdef PROTOBUF_FORCE_COPY_DEFAULT_STRING
        if (_impl_.bytes_.IsDefault()) {
          _impl_.bytes_.Set("", GetArena());
        }
  #endif  // PROTOBUF_FORCE_COPY_DEFAULT_STRING
  // @@protoc_insertion_point(field_set_allocated:tritium.core.CASObjectID.bytes)
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif  // __GNUC__

// @@protoc_insertion_point(namespace_scope)
}  // namespace core
}  // namespace tritium


// @@protoc_insertion_point(global_scope)

#include "google/protobuf/port_undef.inc"

#endif  // GOOGLE_PROTOBUF_INCLUDED_tritium_2fcore_2fCASObjectID_2eproto_2epb_2eh
