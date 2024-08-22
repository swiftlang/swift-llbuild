// Generated by the protocol buffer compiler.  DO NOT EDIT!
// NO CHECKED-IN PROTOBUF GENCODE
// source: tritium/Error.proto
// Protobuf C++ Version: 5.27.2

#include "tritium/Error.pb.h"

#include <algorithm>
#include <type_traits>
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/generated_message_tctable_impl.h"
#include "google/protobuf/extension_set.h"
#include "google/protobuf/wire_format_lite.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/generated_message_reflection.h"
#include "google/protobuf/reflection_ops.h"
#include "google/protobuf/wire_format.h"
// @@protoc_insertion_point(includes)

// Must be included last.
#include "google/protobuf/port_def.inc"
PROTOBUF_PRAGMA_INIT_SEG
namespace _pb = ::google::protobuf;
namespace _pbi = ::google::protobuf::internal;
namespace _fl = ::google::protobuf::internal::field_layout;
namespace tritium {

inline constexpr Error::Impl_::Impl_(
    ::_pbi::ConstantInitialized) noexcept
      : _cached_size_{0},
        context_{},
        description_(
            &::google::protobuf::internal::fixed_address_empty_string,
            ::_pbi::ConstantInitialized()),
        code_{::uint64_t{0u}},
        type_{static_cast< ::tritium::ErrorType >(0)} {}

template <typename>
PROTOBUF_CONSTEXPR Error::Error(::_pbi::ConstantInitialized)
    : _impl_(::_pbi::ConstantInitialized()) {}
struct ErrorDefaultTypeInternal {
  PROTOBUF_CONSTEXPR ErrorDefaultTypeInternal() : _instance(::_pbi::ConstantInitialized{}) {}
  ~ErrorDefaultTypeInternal() {}
  union {
    Error _instance;
  };
};

PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT
    PROTOBUF_ATTRIBUTE_INIT_PRIORITY1 ErrorDefaultTypeInternal _Error_default_instance_;
}  // namespace tritium
static const ::_pb::EnumDescriptor* file_level_enum_descriptors_tritium_2fError_2eproto[1];
static constexpr const ::_pb::ServiceDescriptor**
    file_level_service_descriptors_tritium_2fError_2eproto = nullptr;
const ::uint32_t
    TableStruct_tritium_2fError_2eproto::offsets[] ABSL_ATTRIBUTE_SECTION_VARIABLE(
        protodesc_cold) = {
        PROTOBUF_FIELD_OFFSET(::tritium::Error, _impl_._has_bits_),
        PROTOBUF_FIELD_OFFSET(::tritium::Error, _internal_metadata_),
        ~0u,  // no _extensions_
        ~0u,  // no _oneof_case_
        ~0u,  // no _weak_field_map_
        ~0u,  // no _inlined_string_donated_
        ~0u,  // no _split_
        ~0u,  // no sizeof(Split)
        PROTOBUF_FIELD_OFFSET(::tritium::Error, _impl_.type_),
        PROTOBUF_FIELD_OFFSET(::tritium::Error, _impl_.code_),
        PROTOBUF_FIELD_OFFSET(::tritium::Error, _impl_.description_),
        PROTOBUF_FIELD_OFFSET(::tritium::Error, _impl_.context_),
        ~0u,
        1,
        0,
        ~0u,
};

static const ::_pbi::MigrationSchema
    schemas[] ABSL_ATTRIBUTE_SECTION_VARIABLE(protodesc_cold) = {
        {0, 12, -1, sizeof(::tritium::Error)},
};
static const ::_pb::Message* const file_default_instances[] = {
    &::tritium::_Error_default_instance_._instance,
};
const char descriptor_table_protodef_tritium_2fError_2eproto[] ABSL_ATTRIBUTE_SECTION_VARIABLE(
    protodesc_cold) = {
    "\n\023tritium/Error.proto\022\007tritium\032\031google/p"
    "rotobuf/any.proto\"\226\001\n\005Error\022 \n\004type\030\001 \001("
    "\0162\022.tritium.ErrorType\022\021\n\004code\030\002 \001(\004H\000\210\001\001"
    "\022\030\n\013description\030\003 \001(\tH\001\210\001\001\022%\n\007context\030\004 "
    "\003(\0132\024.google.protobuf.AnyB\007\n\005_codeB\016\n\014_d"
    "escription*<\n\tErrorType\022\n\n\006ENGINE\020\000\022\t\n\005C"
    "ACHE\020\001\022\014\n\010EXECUTOR\020\002\022\n\n\006CLIENT\020\003b\006proto3"
};
static const ::_pbi::DescriptorTable* const descriptor_table_tritium_2fError_2eproto_deps[1] =
    {
        &::descriptor_table_google_2fprotobuf_2fany_2eproto,
};
static ::absl::once_flag descriptor_table_tritium_2fError_2eproto_once;
PROTOBUF_CONSTINIT const ::_pbi::DescriptorTable descriptor_table_tritium_2fError_2eproto = {
    false,
    false,
    280,
    descriptor_table_protodef_tritium_2fError_2eproto,
    "tritium/Error.proto",
    &descriptor_table_tritium_2fError_2eproto_once,
    descriptor_table_tritium_2fError_2eproto_deps,
    1,
    1,
    schemas,
    file_default_instances,
    TableStruct_tritium_2fError_2eproto::offsets,
    file_level_enum_descriptors_tritium_2fError_2eproto,
    file_level_service_descriptors_tritium_2fError_2eproto,
};
namespace tritium {
const ::google::protobuf::EnumDescriptor* ErrorType_descriptor() {
  ::google::protobuf::internal::AssignDescriptors(&descriptor_table_tritium_2fError_2eproto);
  return file_level_enum_descriptors_tritium_2fError_2eproto[0];
}
PROTOBUF_CONSTINIT const uint32_t ErrorType_internal_data_[] = {
    262144u, 0u, };
bool ErrorType_IsValid(int value) {
  return 0 <= value && value <= 3;
}
// ===================================================================

class Error::_Internal {
 public:
  using HasBits =
      decltype(std::declval<Error>()._impl_._has_bits_);
  static constexpr ::int32_t kHasBitsOffset =
      8 * PROTOBUF_FIELD_OFFSET(Error, _impl_._has_bits_);
};

void Error::clear_context() {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  _impl_.context_.Clear();
}
Error::Error(::google::protobuf::Arena* arena)
    : ::google::protobuf::Message(arena) {
  SharedCtor(arena);
  // @@protoc_insertion_point(arena_constructor:tritium.Error)
}
inline PROTOBUF_NDEBUG_INLINE Error::Impl_::Impl_(
    ::google::protobuf::internal::InternalVisibility visibility, ::google::protobuf::Arena* arena,
    const Impl_& from, const ::tritium::Error& from_msg)
      : _has_bits_{from._has_bits_},
        _cached_size_{0},
        context_{visibility, arena, from.context_},
        description_(arena, from.description_) {}

Error::Error(
    ::google::protobuf::Arena* arena,
    const Error& from)
    : ::google::protobuf::Message(arena) {
  Error* const _this = this;
  (void)_this;
  _internal_metadata_.MergeFrom<::google::protobuf::UnknownFieldSet>(
      from._internal_metadata_);
  new (&_impl_) Impl_(internal_visibility(), arena, from._impl_, from);
  ::memcpy(reinterpret_cast<char *>(&_impl_) +
               offsetof(Impl_, code_),
           reinterpret_cast<const char *>(&from._impl_) +
               offsetof(Impl_, code_),
           offsetof(Impl_, type_) -
               offsetof(Impl_, code_) +
               sizeof(Impl_::type_));

  // @@protoc_insertion_point(copy_constructor:tritium.Error)
}
inline PROTOBUF_NDEBUG_INLINE Error::Impl_::Impl_(
    ::google::protobuf::internal::InternalVisibility visibility,
    ::google::protobuf::Arena* arena)
      : _cached_size_{0},
        context_{visibility, arena},
        description_(arena) {}

inline void Error::SharedCtor(::_pb::Arena* arena) {
  new (&_impl_) Impl_(internal_visibility(), arena);
  ::memset(reinterpret_cast<char *>(&_impl_) +
               offsetof(Impl_, code_),
           0,
           offsetof(Impl_, type_) -
               offsetof(Impl_, code_) +
               sizeof(Impl_::type_));
}
Error::~Error() {
  // @@protoc_insertion_point(destructor:tritium.Error)
  _internal_metadata_.Delete<::google::protobuf::UnknownFieldSet>();
  SharedDtor();
}
inline void Error::SharedDtor() {
  ABSL_DCHECK(GetArena() == nullptr);
  _impl_.description_.Destroy();
  _impl_.~Impl_();
}

const ::google::protobuf::MessageLite::ClassData*
Error::GetClassData() const {
  PROTOBUF_CONSTINIT static const ::google::protobuf::MessageLite::
      ClassDataFull _data_ = {
          {
              &_table_.header,
              nullptr,  // OnDemandRegisterArenaDtor
              nullptr,  // IsInitialized
              PROTOBUF_FIELD_OFFSET(Error, _impl_._cached_size_),
              false,
          },
          &Error::MergeImpl,
          &Error::kDescriptorMethods,
          &descriptor_table_tritium_2fError_2eproto,
          nullptr,  // tracker
      };
  ::google::protobuf::internal::PrefetchToLocalCache(&_data_);
  ::google::protobuf::internal::PrefetchToLocalCache(_data_.tc_table);
  return _data_.base();
}
PROTOBUF_CONSTINIT PROTOBUF_ATTRIBUTE_INIT_PRIORITY1
const ::_pbi::TcParseTable<2, 4, 1, 33, 2> Error::_table_ = {
  {
    PROTOBUF_FIELD_OFFSET(Error, _impl_._has_bits_),
    0, // no _extensions_
    4, 24,  // max_field_number, fast_idx_mask
    offsetof(decltype(_table_), field_lookup_table),
    4294967280,  // skipmap
    offsetof(decltype(_table_), field_entries),
    4,  // num_field_entries
    1,  // num_aux_entries
    offsetof(decltype(_table_), aux_entries),
    &_Error_default_instance_._instance,
    nullptr,  // post_loop_handler
    ::_pbi::TcParser::GenericFallback,  // fallback
    #ifdef PROTOBUF_PREFETCH_PARSE_TABLE
    ::_pbi::TcParser::GetTable<::tritium::Error>(),  // to_prefetch
    #endif  // PROTOBUF_PREFETCH_PARSE_TABLE
  }, {{
    // repeated .google.protobuf.Any context = 4;
    {::_pbi::TcParser::FastMtR1,
     {34, 63, 0, PROTOBUF_FIELD_OFFSET(Error, _impl_.context_)}},
    // .tritium.ErrorType type = 1;
    {::_pbi::TcParser::SingularVarintNoZag1<::uint32_t, offsetof(Error, _impl_.type_), 63>(),
     {8, 63, 0, PROTOBUF_FIELD_OFFSET(Error, _impl_.type_)}},
    // optional uint64 code = 2;
    {::_pbi::TcParser::SingularVarintNoZag1<::uint64_t, offsetof(Error, _impl_.code_), 1>(),
     {16, 1, 0, PROTOBUF_FIELD_OFFSET(Error, _impl_.code_)}},
    // optional string description = 3;
    {::_pbi::TcParser::FastUS1,
     {26, 0, 0, PROTOBUF_FIELD_OFFSET(Error, _impl_.description_)}},
  }}, {{
    65535, 65535
  }}, {{
    // .tritium.ErrorType type = 1;
    {PROTOBUF_FIELD_OFFSET(Error, _impl_.type_), -1, 0,
    (0 | ::_fl::kFcSingular | ::_fl::kOpenEnum)},
    // optional uint64 code = 2;
    {PROTOBUF_FIELD_OFFSET(Error, _impl_.code_), _Internal::kHasBitsOffset + 1, 0,
    (0 | ::_fl::kFcOptional | ::_fl::kUInt64)},
    // optional string description = 3;
    {PROTOBUF_FIELD_OFFSET(Error, _impl_.description_), _Internal::kHasBitsOffset + 0, 0,
    (0 | ::_fl::kFcOptional | ::_fl::kUtf8String | ::_fl::kRepAString)},
    // repeated .google.protobuf.Any context = 4;
    {PROTOBUF_FIELD_OFFSET(Error, _impl_.context_), -1, 0,
    (0 | ::_fl::kFcRepeated | ::_fl::kMessage | ::_fl::kTvTable)},
  }}, {{
    {::_pbi::TcParser::GetTable<::google::protobuf::Any>()},
  }}, {{
    "\15\0\0\13\0\0\0\0"
    "tritium.Error"
    "description"
  }},
};

PROTOBUF_NOINLINE void Error::Clear() {
// @@protoc_insertion_point(message_clear_start:tritium.Error)
  ::google::protobuf::internal::TSanWrite(&_impl_);
  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  _impl_.context_.Clear();
  cached_has_bits = _impl_._has_bits_[0];
  if (cached_has_bits & 0x00000001u) {
    _impl_.description_.ClearNonDefaultToEmpty();
  }
  _impl_.code_ = ::uint64_t{0u};
  _impl_.type_ = 0;
  _impl_._has_bits_.Clear();
  _internal_metadata_.Clear<::google::protobuf::UnknownFieldSet>();
}

::uint8_t* Error::_InternalSerialize(
    ::uint8_t* target,
    ::google::protobuf::io::EpsCopyOutputStream* stream) const {
  // @@protoc_insertion_point(serialize_to_array_start:tritium.Error)
  ::uint32_t cached_has_bits = 0;
  (void)cached_has_bits;

  // .tritium.ErrorType type = 1;
  if (this->_internal_type() != 0) {
    target = stream->EnsureSpace(target);
    target = ::_pbi::WireFormatLite::WriteEnumToArray(
        1, this->_internal_type(), target);
  }

  cached_has_bits = _impl_._has_bits_[0];
  // optional uint64 code = 2;
  if (cached_has_bits & 0x00000002u) {
    target = stream->EnsureSpace(target);
    target = ::_pbi::WireFormatLite::WriteUInt64ToArray(
        2, this->_internal_code(), target);
  }

  // optional string description = 3;
  if (cached_has_bits & 0x00000001u) {
    const std::string& _s = this->_internal_description();
    ::google::protobuf::internal::WireFormatLite::VerifyUtf8String(
        _s.data(), static_cast<int>(_s.length()), ::google::protobuf::internal::WireFormatLite::SERIALIZE, "tritium.Error.description");
    target = stream->WriteStringMaybeAliased(3, _s, target);
  }

  // repeated .google.protobuf.Any context = 4;
  for (unsigned i = 0, n = static_cast<unsigned>(
                           this->_internal_context_size());
       i < n; i++) {
    const auto& repfield = this->_internal_context().Get(i);
    target =
        ::google::protobuf::internal::WireFormatLite::InternalWriteMessage(
            4, repfield, repfield.GetCachedSize(),
            target, stream);
  }

  if (PROTOBUF_PREDICT_FALSE(_internal_metadata_.have_unknown_fields())) {
    target =
        ::_pbi::WireFormat::InternalSerializeUnknownFieldsToArray(
            _internal_metadata_.unknown_fields<::google::protobuf::UnknownFieldSet>(::google::protobuf::UnknownFieldSet::default_instance), target, stream);
  }
  // @@protoc_insertion_point(serialize_to_array_end:tritium.Error)
  return target;
}

::size_t Error::ByteSizeLong() const {
// @@protoc_insertion_point(message_byte_size_start:tritium.Error)
  ::size_t total_size = 0;

  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  ::_pbi::Prefetch5LinesFrom7Lines(reinterpret_cast<const void*>(this));
  // repeated .google.protobuf.Any context = 4;
  total_size += 1UL * this->_internal_context_size();
  for (const auto& msg : this->_internal_context()) {
    total_size += ::google::protobuf::internal::WireFormatLite::MessageSize(msg);
  }
  cached_has_bits = _impl_._has_bits_[0];
  if (cached_has_bits & 0x00000003u) {
    // optional string description = 3;
    if (cached_has_bits & 0x00000001u) {
      total_size += 1 + ::google::protobuf::internal::WireFormatLite::StringSize(
                                      this->_internal_description());
    }

    // optional uint64 code = 2;
    if (cached_has_bits & 0x00000002u) {
      total_size += ::_pbi::WireFormatLite::UInt64SizePlusOne(
          this->_internal_code());
    }

  }
  // .tritium.ErrorType type = 1;
  if (this->_internal_type() != 0) {
    total_size += 1 +
                  ::_pbi::WireFormatLite::EnumSize(this->_internal_type());
  }

  return MaybeComputeUnknownFieldsSize(total_size, &_impl_._cached_size_);
}


void Error::MergeImpl(::google::protobuf::MessageLite& to_msg, const ::google::protobuf::MessageLite& from_msg) {
  auto* const _this = static_cast<Error*>(&to_msg);
  auto& from = static_cast<const Error&>(from_msg);
  // @@protoc_insertion_point(class_specific_merge_from_start:tritium.Error)
  ABSL_DCHECK_NE(&from, _this);
  ::uint32_t cached_has_bits = 0;
  (void) cached_has_bits;

  _this->_internal_mutable_context()->MergeFrom(
      from._internal_context());
  cached_has_bits = from._impl_._has_bits_[0];
  if (cached_has_bits & 0x00000003u) {
    if (cached_has_bits & 0x00000001u) {
      _this->_internal_set_description(from._internal_description());
    }
    if (cached_has_bits & 0x00000002u) {
      _this->_impl_.code_ = from._impl_.code_;
    }
  }
  if (from._internal_type() != 0) {
    _this->_impl_.type_ = from._impl_.type_;
  }
  _this->_impl_._has_bits_[0] |= cached_has_bits;
  _this->_internal_metadata_.MergeFrom<::google::protobuf::UnknownFieldSet>(from._internal_metadata_);
}

void Error::CopyFrom(const Error& from) {
// @@protoc_insertion_point(class_specific_copy_from_start:tritium.Error)
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}


void Error::InternalSwap(Error* PROTOBUF_RESTRICT other) {
  using std::swap;
  auto* arena = GetArena();
  ABSL_DCHECK_EQ(arena, other->GetArena());
  _internal_metadata_.InternalSwap(&other->_internal_metadata_);
  swap(_impl_._has_bits_[0], other->_impl_._has_bits_[0]);
  _impl_.context_.InternalSwap(&other->_impl_.context_);
  ::_pbi::ArenaStringPtr::InternalSwap(&_impl_.description_, &other->_impl_.description_, arena);
  ::google::protobuf::internal::memswap<
      PROTOBUF_FIELD_OFFSET(Error, _impl_.type_)
      + sizeof(Error::_impl_.type_)
      - PROTOBUF_FIELD_OFFSET(Error, _impl_.code_)>(
          reinterpret_cast<char*>(&_impl_.code_),
          reinterpret_cast<char*>(&other->_impl_.code_));
}

::google::protobuf::Metadata Error::GetMetadata() const {
  return ::google::protobuf::Message::GetMetadataImpl(GetClassData()->full());
}
// @@protoc_insertion_point(namespace_scope)
}  // namespace tritium
namespace google {
namespace protobuf {
}  // namespace protobuf
}  // namespace google
// @@protoc_insertion_point(global_scope)
PROTOBUF_ATTRIBUTE_INIT_PRIORITY2 static ::std::false_type
    _static_init2_ PROTOBUF_UNUSED =
        (::_pbi::AddDescriptors(&descriptor_table_tritium_2fError_2eproto),
         ::std::false_type{});
#include "google/protobuf/port_undef.inc"
