// Generated by the protocol buffer compiler.  DO NOT EDIT!
// NO CHECKED-IN PROTOBUF GENCODE
// source: llbuild3/Common.proto
// Protobuf C++ Version: 5.27.2

#include "llbuild3/Common.pb.h"

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
namespace llbuild3 {

inline constexpr Stat::Impl_::Impl_(
    ::_pbi::ConstantInitialized) noexcept
      : name_(
            &::google::protobuf::internal::fixed_address_empty_string,
            ::_pbi::ConstantInitialized()),
        value_{},
        _cached_size_{0},
        _oneof_case_{} {}

template <typename>
PROTOBUF_CONSTEXPR Stat::Stat(::_pbi::ConstantInitialized)
    : _impl_(::_pbi::ConstantInitialized()) {}
struct StatDefaultTypeInternal {
  PROTOBUF_CONSTEXPR StatDefaultTypeInternal() : _instance(::_pbi::ConstantInitialized{}) {}
  ~StatDefaultTypeInternal() {}
  union {
    Stat _instance;
  };
};

PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT
    PROTOBUF_ATTRIBUTE_INIT_PRIORITY1 StatDefaultTypeInternal _Stat_default_instance_;
}  // namespace llbuild3
static constexpr const ::_pb::EnumDescriptor**
    file_level_enum_descriptors_llbuild3_2fCommon_2eproto = nullptr;
static constexpr const ::_pb::ServiceDescriptor**
    file_level_service_descriptors_llbuild3_2fCommon_2eproto = nullptr;
const ::uint32_t
    TableStruct_llbuild3_2fCommon_2eproto::offsets[] ABSL_ATTRIBUTE_SECTION_VARIABLE(
        protodesc_cold) = {
        ~0u,  // no _has_bits_
        PROTOBUF_FIELD_OFFSET(::llbuild3::Stat, _internal_metadata_),
        ~0u,  // no _extensions_
        PROTOBUF_FIELD_OFFSET(::llbuild3::Stat, _impl_._oneof_case_[0]),
        ~0u,  // no _weak_field_map_
        ~0u,  // no _inlined_string_donated_
        ~0u,  // no _split_
        ~0u,  // no sizeof(Split)
        PROTOBUF_FIELD_OFFSET(::llbuild3::Stat, _impl_.name_),
        ::_pbi::kInvalidFieldOffsetTag,
        ::_pbi::kInvalidFieldOffsetTag,
        ::_pbi::kInvalidFieldOffsetTag,
        ::_pbi::kInvalidFieldOffsetTag,
        ::_pbi::kInvalidFieldOffsetTag,
        ::_pbi::kInvalidFieldOffsetTag,
        ::_pbi::kInvalidFieldOffsetTag,
        PROTOBUF_FIELD_OFFSET(::llbuild3::Stat, _impl_.value_),
};

static const ::_pbi::MigrationSchema
    schemas[] ABSL_ATTRIBUTE_SECTION_VARIABLE(protodesc_cold) = {
        {0, -1, -1, sizeof(::llbuild3::Stat)},
};
static const ::_pb::Message* const file_default_instances[] = {
    &::llbuild3::_Stat_default_instance_._instance,
};
const char descriptor_table_protodef_llbuild3_2fCommon_2eproto[] ABSL_ATTRIBUTE_SECTION_VARIABLE(
    protodesc_cold) = {
    "\n\025llbuild3/Common.proto\022\010llbuild3\032\022llbui"
    "ld3/CAS.proto\032\024llbuild3/Error.proto\"\335\001\n\004"
    "Stat\022\014\n\004name\030\001 \001(\t\022\023\n\tint_value\030\002 \001(\003H\000\022"
    "\024\n\nuint_value\030\003 \001(\004H\000\022\026\n\014string_value\030\004 "
    "\001(\tH\000\022\024\n\nbool_value\030\005 \001(\010H\000\022\026\n\014double_va"
    "lue\030\006 \001(\001H\000\022%\n\ncas_object\030\007 \001(\0132\017.llbuil"
    "d3.CASIDH\000\022&\n\013error_value\030\010 \001(\0132\017.llbuil"
    "d3.ErrorH\000B\007\n\005valueb\006proto3"
};
static const ::_pbi::DescriptorTable* const descriptor_table_llbuild3_2fCommon_2eproto_deps[2] =
    {
        &::descriptor_table_llbuild3_2fCAS_2eproto,
        &::descriptor_table_llbuild3_2fError_2eproto,
};
static ::absl::once_flag descriptor_table_llbuild3_2fCommon_2eproto_once;
PROTOBUF_CONSTINIT const ::_pbi::DescriptorTable descriptor_table_llbuild3_2fCommon_2eproto = {
    false,
    false,
    307,
    descriptor_table_protodef_llbuild3_2fCommon_2eproto,
    "llbuild3/Common.proto",
    &descriptor_table_llbuild3_2fCommon_2eproto_once,
    descriptor_table_llbuild3_2fCommon_2eproto_deps,
    2,
    1,
    schemas,
    file_default_instances,
    TableStruct_llbuild3_2fCommon_2eproto::offsets,
    file_level_enum_descriptors_llbuild3_2fCommon_2eproto,
    file_level_service_descriptors_llbuild3_2fCommon_2eproto,
};
namespace llbuild3 {
// ===================================================================

class Stat::_Internal {
 public:
  static constexpr ::int32_t kOneofCaseOffset =
      PROTOBUF_FIELD_OFFSET(::llbuild3::Stat, _impl_._oneof_case_);
};

void Stat::set_allocated_cas_object(::llbuild3::CASID* cas_object) {
  ::google::protobuf::Arena* message_arena = GetArena();
  clear_value();
  if (cas_object) {
    ::google::protobuf::Arena* submessage_arena = reinterpret_cast<::google::protobuf::MessageLite*>(cas_object)->GetArena();
    if (message_arena != submessage_arena) {
      cas_object = ::google::protobuf::internal::GetOwnedMessage(message_arena, cas_object, submessage_arena);
    }
    set_has_cas_object();
    _impl_.value_.cas_object_ = cas_object;
  }
  // @@protoc_insertion_point(field_set_allocated:llbuild3.Stat.cas_object)
}
void Stat::clear_cas_object() {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  if (value_case() == kCasObject) {
    if (GetArena() == nullptr) {
      delete _impl_.value_.cas_object_;
    } else if (::google::protobuf::internal::DebugHardenClearOneofMessageOnArena()) {
      ::google::protobuf::internal::MaybePoisonAfterClear(_impl_.value_.cas_object_);
    }
    clear_has_value();
  }
}
void Stat::set_allocated_error_value(::llbuild3::Error* error_value) {
  ::google::protobuf::Arena* message_arena = GetArena();
  clear_value();
  if (error_value) {
    ::google::protobuf::Arena* submessage_arena = reinterpret_cast<::google::protobuf::MessageLite*>(error_value)->GetArena();
    if (message_arena != submessage_arena) {
      error_value = ::google::protobuf::internal::GetOwnedMessage(message_arena, error_value, submessage_arena);
    }
    set_has_error_value();
    _impl_.value_.error_value_ = error_value;
  }
  // @@protoc_insertion_point(field_set_allocated:llbuild3.Stat.error_value)
}
void Stat::clear_error_value() {
  ::google::protobuf::internal::TSanWrite(&_impl_);
  if (value_case() == kErrorValue) {
    if (GetArena() == nullptr) {
      delete _impl_.value_.error_value_;
    } else if (::google::protobuf::internal::DebugHardenClearOneofMessageOnArena()) {
      ::google::protobuf::internal::MaybePoisonAfterClear(_impl_.value_.error_value_);
    }
    clear_has_value();
  }
}
Stat::Stat(::google::protobuf::Arena* arena)
    : ::google::protobuf::Message(arena) {
  SharedCtor(arena);
  // @@protoc_insertion_point(arena_constructor:llbuild3.Stat)
}
inline PROTOBUF_NDEBUG_INLINE Stat::Impl_::Impl_(
    ::google::protobuf::internal::InternalVisibility visibility, ::google::protobuf::Arena* arena,
    const Impl_& from, const ::llbuild3::Stat& from_msg)
      : name_(arena, from.name_),
        value_{},
        _cached_size_{0},
        _oneof_case_{from._oneof_case_[0]} {}

Stat::Stat(
    ::google::protobuf::Arena* arena,
    const Stat& from)
    : ::google::protobuf::Message(arena) {
  Stat* const _this = this;
  (void)_this;
  _internal_metadata_.MergeFrom<::google::protobuf::UnknownFieldSet>(
      from._internal_metadata_);
  new (&_impl_) Impl_(internal_visibility(), arena, from._impl_, from);
  switch (value_case()) {
    case VALUE_NOT_SET:
      break;
      case kIntValue:
        _impl_.value_.int_value_ = from._impl_.value_.int_value_;
        break;
      case kUintValue:
        _impl_.value_.uint_value_ = from._impl_.value_.uint_value_;
        break;
      case kStringValue:
        new (&_impl_.value_.string_value_) decltype(_impl_.value_.string_value_){arena, from._impl_.value_.string_value_};
        break;
      case kBoolValue:
        _impl_.value_.bool_value_ = from._impl_.value_.bool_value_;
        break;
      case kDoubleValue:
        _impl_.value_.double_value_ = from._impl_.value_.double_value_;
        break;
      case kCasObject:
        _impl_.value_.cas_object_ = ::google::protobuf::Message::CopyConstruct<::llbuild3::CASID>(arena, *from._impl_.value_.cas_object_);
        break;
      case kErrorValue:
        _impl_.value_.error_value_ = ::google::protobuf::Message::CopyConstruct<::llbuild3::Error>(arena, *from._impl_.value_.error_value_);
        break;
  }

  // @@protoc_insertion_point(copy_constructor:llbuild3.Stat)
}
inline PROTOBUF_NDEBUG_INLINE Stat::Impl_::Impl_(
    ::google::protobuf::internal::InternalVisibility visibility,
    ::google::protobuf::Arena* arena)
      : name_(arena),
        value_{},
        _cached_size_{0},
        _oneof_case_{} {}

inline void Stat::SharedCtor(::_pb::Arena* arena) {
  new (&_impl_) Impl_(internal_visibility(), arena);
}
Stat::~Stat() {
  // @@protoc_insertion_point(destructor:llbuild3.Stat)
  _internal_metadata_.Delete<::google::protobuf::UnknownFieldSet>();
  SharedDtor();
}
inline void Stat::SharedDtor() {
  ABSL_DCHECK(GetArena() == nullptr);
  _impl_.name_.Destroy();
  if (has_value()) {
    clear_value();
  }
  _impl_.~Impl_();
}

void Stat::clear_value() {
// @@protoc_insertion_point(one_of_clear_start:llbuild3.Stat)
  ::google::protobuf::internal::TSanWrite(&_impl_);
  switch (value_case()) {
    case kIntValue: {
      // No need to clear
      break;
    }
    case kUintValue: {
      // No need to clear
      break;
    }
    case kStringValue: {
      _impl_.value_.string_value_.Destroy();
      break;
    }
    case kBoolValue: {
      // No need to clear
      break;
    }
    case kDoubleValue: {
      // No need to clear
      break;
    }
    case kCasObject: {
      if (GetArena() == nullptr) {
        delete _impl_.value_.cas_object_;
      } else if (::google::protobuf::internal::DebugHardenClearOneofMessageOnArena()) {
        ::google::protobuf::internal::MaybePoisonAfterClear(_impl_.value_.cas_object_);
      }
      break;
    }
    case kErrorValue: {
      if (GetArena() == nullptr) {
        delete _impl_.value_.error_value_;
      } else if (::google::protobuf::internal::DebugHardenClearOneofMessageOnArena()) {
        ::google::protobuf::internal::MaybePoisonAfterClear(_impl_.value_.error_value_);
      }
      break;
    }
    case VALUE_NOT_SET: {
      break;
    }
  }
  _impl_._oneof_case_[0] = VALUE_NOT_SET;
}


const ::google::protobuf::MessageLite::ClassData*
Stat::GetClassData() const {
  PROTOBUF_CONSTINIT static const ::google::protobuf::MessageLite::
      ClassDataFull _data_ = {
          {
              &_table_.header,
              nullptr,  // OnDemandRegisterArenaDtor
              nullptr,  // IsInitialized
              PROTOBUF_FIELD_OFFSET(Stat, _impl_._cached_size_),
              false,
          },
          &Stat::MergeImpl,
          &Stat::kDescriptorMethods,
          &descriptor_table_llbuild3_2fCommon_2eproto,
          nullptr,  // tracker
      };
  ::google::protobuf::internal::PrefetchToLocalCache(&_data_);
  ::google::protobuf::internal::PrefetchToLocalCache(_data_.tc_table);
  return _data_.base();
}
PROTOBUF_CONSTINIT PROTOBUF_ATTRIBUTE_INIT_PRIORITY1
const ::_pbi::TcParseTable<0, 8, 2, 46, 2> Stat::_table_ = {
  {
    0,  // no _has_bits_
    0, // no _extensions_
    8, 0,  // max_field_number, fast_idx_mask
    offsetof(decltype(_table_), field_lookup_table),
    4294967040,  // skipmap
    offsetof(decltype(_table_), field_entries),
    8,  // num_field_entries
    2,  // num_aux_entries
    offsetof(decltype(_table_), aux_entries),
    &_Stat_default_instance_._instance,
    nullptr,  // post_loop_handler
    ::_pbi::TcParser::GenericFallback,  // fallback
    #ifdef PROTOBUF_PREFETCH_PARSE_TABLE
    ::_pbi::TcParser::GetTable<::llbuild3::Stat>(),  // to_prefetch
    #endif  // PROTOBUF_PREFETCH_PARSE_TABLE
  }, {{
    // string name = 1;
    {::_pbi::TcParser::FastUS1,
     {10, 63, 0, PROTOBUF_FIELD_OFFSET(Stat, _impl_.name_)}},
  }}, {{
    65535, 65535
  }}, {{
    // string name = 1;
    {PROTOBUF_FIELD_OFFSET(Stat, _impl_.name_), 0, 0,
    (0 | ::_fl::kFcSingular | ::_fl::kUtf8String | ::_fl::kRepAString)},
    // int64 int_value = 2;
    {PROTOBUF_FIELD_OFFSET(Stat, _impl_.value_.int_value_), _Internal::kOneofCaseOffset + 0, 0,
    (0 | ::_fl::kFcOneof | ::_fl::kInt64)},
    // uint64 uint_value = 3;
    {PROTOBUF_FIELD_OFFSET(Stat, _impl_.value_.uint_value_), _Internal::kOneofCaseOffset + 0, 0,
    (0 | ::_fl::kFcOneof | ::_fl::kUInt64)},
    // string string_value = 4;
    {PROTOBUF_FIELD_OFFSET(Stat, _impl_.value_.string_value_), _Internal::kOneofCaseOffset + 0, 0,
    (0 | ::_fl::kFcOneof | ::_fl::kUtf8String | ::_fl::kRepAString)},
    // bool bool_value = 5;
    {PROTOBUF_FIELD_OFFSET(Stat, _impl_.value_.bool_value_), _Internal::kOneofCaseOffset + 0, 0,
    (0 | ::_fl::kFcOneof | ::_fl::kBool)},
    // double double_value = 6;
    {PROTOBUF_FIELD_OFFSET(Stat, _impl_.value_.double_value_), _Internal::kOneofCaseOffset + 0, 0,
    (0 | ::_fl::kFcOneof | ::_fl::kDouble)},
    // .llbuild3.CASID cas_object = 7;
    {PROTOBUF_FIELD_OFFSET(Stat, _impl_.value_.cas_object_), _Internal::kOneofCaseOffset + 0, 0,
    (0 | ::_fl::kFcOneof | ::_fl::kMessage | ::_fl::kTvTable)},
    // .llbuild3.Error error_value = 8;
    {PROTOBUF_FIELD_OFFSET(Stat, _impl_.value_.error_value_), _Internal::kOneofCaseOffset + 0, 1,
    (0 | ::_fl::kFcOneof | ::_fl::kMessage | ::_fl::kTvTable)},
  }}, {{
    {::_pbi::TcParser::GetTable<::llbuild3::CASID>()},
    {::_pbi::TcParser::GetTable<::llbuild3::Error>()},
  }}, {{
    "\15\4\0\0\14\0\0\0\0\0\0\0\0\0\0\0"
    "llbuild3.Stat"
    "name"
    "string_value"
  }},
};

PROTOBUF_NOINLINE void Stat::Clear() {
// @@protoc_insertion_point(message_clear_start:llbuild3.Stat)
  ::google::protobuf::internal::TSanWrite(&_impl_);
  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  _impl_.name_.ClearToEmpty();
  clear_value();
  _internal_metadata_.Clear<::google::protobuf::UnknownFieldSet>();
}

::uint8_t* Stat::_InternalSerialize(
    ::uint8_t* target,
    ::google::protobuf::io::EpsCopyOutputStream* stream) const {
  // @@protoc_insertion_point(serialize_to_array_start:llbuild3.Stat)
  ::uint32_t cached_has_bits = 0;
  (void)cached_has_bits;

  // string name = 1;
  if (!this->_internal_name().empty()) {
    const std::string& _s = this->_internal_name();
    ::google::protobuf::internal::WireFormatLite::VerifyUtf8String(
        _s.data(), static_cast<int>(_s.length()), ::google::protobuf::internal::WireFormatLite::SERIALIZE, "llbuild3.Stat.name");
    target = stream->WriteStringMaybeAliased(1, _s, target);
  }

  switch (value_case()) {
    case kIntValue: {
      target = ::google::protobuf::internal::WireFormatLite::
          WriteInt64ToArrayWithField<2>(
              stream, this->_internal_int_value(), target);
      break;
    }
    case kUintValue: {
      target = stream->EnsureSpace(target);
      target = ::_pbi::WireFormatLite::WriteUInt64ToArray(
          3, this->_internal_uint_value(), target);
      break;
    }
    case kStringValue: {
      const std::string& _s = this->_internal_string_value();
      ::google::protobuf::internal::WireFormatLite::VerifyUtf8String(
          _s.data(), static_cast<int>(_s.length()), ::google::protobuf::internal::WireFormatLite::SERIALIZE, "llbuild3.Stat.string_value");
      target = stream->WriteStringMaybeAliased(4, _s, target);
      break;
    }
    case kBoolValue: {
      target = stream->EnsureSpace(target);
      target = ::_pbi::WireFormatLite::WriteBoolToArray(
          5, this->_internal_bool_value(), target);
      break;
    }
    case kDoubleValue: {
      target = stream->EnsureSpace(target);
      target = ::_pbi::WireFormatLite::WriteDoubleToArray(
          6, this->_internal_double_value(), target);
      break;
    }
    case kCasObject: {
      target = ::google::protobuf::internal::WireFormatLite::InternalWriteMessage(
          7, *_impl_.value_.cas_object_, _impl_.value_.cas_object_->GetCachedSize(), target, stream);
      break;
    }
    case kErrorValue: {
      target = ::google::protobuf::internal::WireFormatLite::InternalWriteMessage(
          8, *_impl_.value_.error_value_, _impl_.value_.error_value_->GetCachedSize(), target, stream);
      break;
    }
    default:
      break;
  }
  if (PROTOBUF_PREDICT_FALSE(_internal_metadata_.have_unknown_fields())) {
    target =
        ::_pbi::WireFormat::InternalSerializeUnknownFieldsToArray(
            _internal_metadata_.unknown_fields<::google::protobuf::UnknownFieldSet>(::google::protobuf::UnknownFieldSet::default_instance), target, stream);
  }
  // @@protoc_insertion_point(serialize_to_array_end:llbuild3.Stat)
  return target;
}

::size_t Stat::ByteSizeLong() const {
// @@protoc_insertion_point(message_byte_size_start:llbuild3.Stat)
  ::size_t total_size = 0;

  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  // string name = 1;
  if (!this->_internal_name().empty()) {
    total_size += 1 + ::google::protobuf::internal::WireFormatLite::StringSize(
                                    this->_internal_name());
  }

  switch (value_case()) {
    // int64 int_value = 2;
    case kIntValue: {
      total_size += ::_pbi::WireFormatLite::Int64SizePlusOne(
          this->_internal_int_value());
      break;
    }
    // uint64 uint_value = 3;
    case kUintValue: {
      total_size += ::_pbi::WireFormatLite::UInt64SizePlusOne(
          this->_internal_uint_value());
      break;
    }
    // string string_value = 4;
    case kStringValue: {
      total_size += 1 + ::google::protobuf::internal::WireFormatLite::StringSize(
                                      this->_internal_string_value());
      break;
    }
    // bool bool_value = 5;
    case kBoolValue: {
      total_size += 2;
      break;
    }
    // double double_value = 6;
    case kDoubleValue: {
      total_size += 9;
      break;
    }
    // .llbuild3.CASID cas_object = 7;
    case kCasObject: {
      total_size +=
          1 + ::google::protobuf::internal::WireFormatLite::MessageSize(*_impl_.value_.cas_object_);
      break;
    }
    // .llbuild3.Error error_value = 8;
    case kErrorValue: {
      total_size +=
          1 + ::google::protobuf::internal::WireFormatLite::MessageSize(*_impl_.value_.error_value_);
      break;
    }
    case VALUE_NOT_SET: {
      break;
    }
  }
  return MaybeComputeUnknownFieldsSize(total_size, &_impl_._cached_size_);
}


void Stat::MergeImpl(::google::protobuf::MessageLite& to_msg, const ::google::protobuf::MessageLite& from_msg) {
  auto* const _this = static_cast<Stat*>(&to_msg);
  auto& from = static_cast<const Stat&>(from_msg);
  ::google::protobuf::Arena* arena = _this->GetArena();
  // @@protoc_insertion_point(class_specific_merge_from_start:llbuild3.Stat)
  ABSL_DCHECK_NE(&from, _this);
  ::uint32_t cached_has_bits = 0;
  (void) cached_has_bits;

  if (!from._internal_name().empty()) {
    _this->_internal_set_name(from._internal_name());
  }
  if (const uint32_t oneof_from_case = from._impl_._oneof_case_[0]) {
    const uint32_t oneof_to_case = _this->_impl_._oneof_case_[0];
    const bool oneof_needs_init = oneof_to_case != oneof_from_case;
    if (oneof_needs_init) {
      if (oneof_to_case != 0) {
        _this->clear_value();
      }
      _this->_impl_._oneof_case_[0] = oneof_from_case;
    }

    switch (oneof_from_case) {
      case kIntValue: {
        _this->_impl_.value_.int_value_ = from._impl_.value_.int_value_;
        break;
      }
      case kUintValue: {
        _this->_impl_.value_.uint_value_ = from._impl_.value_.uint_value_;
        break;
      }
      case kStringValue: {
        if (oneof_needs_init) {
          _this->_impl_.value_.string_value_.InitDefault();
        }
        _this->_impl_.value_.string_value_.Set(from._internal_string_value(), arena);
        break;
      }
      case kBoolValue: {
        _this->_impl_.value_.bool_value_ = from._impl_.value_.bool_value_;
        break;
      }
      case kDoubleValue: {
        _this->_impl_.value_.double_value_ = from._impl_.value_.double_value_;
        break;
      }
      case kCasObject: {
        if (oneof_needs_init) {
          _this->_impl_.value_.cas_object_ =
              ::google::protobuf::Message::CopyConstruct<::llbuild3::CASID>(arena, *from._impl_.value_.cas_object_);
        } else {
          _this->_impl_.value_.cas_object_->MergeFrom(from._internal_cas_object());
        }
        break;
      }
      case kErrorValue: {
        if (oneof_needs_init) {
          _this->_impl_.value_.error_value_ =
              ::google::protobuf::Message::CopyConstruct<::llbuild3::Error>(arena, *from._impl_.value_.error_value_);
        } else {
          _this->_impl_.value_.error_value_->MergeFrom(from._internal_error_value());
        }
        break;
      }
      case VALUE_NOT_SET:
        break;
    }
  }
  _this->_internal_metadata_.MergeFrom<::google::protobuf::UnknownFieldSet>(from._internal_metadata_);
}

void Stat::CopyFrom(const Stat& from) {
// @@protoc_insertion_point(class_specific_copy_from_start:llbuild3.Stat)
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}


void Stat::InternalSwap(Stat* PROTOBUF_RESTRICT other) {
  using std::swap;
  auto* arena = GetArena();
  ABSL_DCHECK_EQ(arena, other->GetArena());
  _internal_metadata_.InternalSwap(&other->_internal_metadata_);
  ::_pbi::ArenaStringPtr::InternalSwap(&_impl_.name_, &other->_impl_.name_, arena);
  swap(_impl_.value_, other->_impl_.value_);
  swap(_impl_._oneof_case_[0], other->_impl_._oneof_case_[0]);
}

::google::protobuf::Metadata Stat::GetMetadata() const {
  return ::google::protobuf::Message::GetMetadataImpl(GetClassData()->full());
}
// @@protoc_insertion_point(namespace_scope)
}  // namespace llbuild3
namespace google {
namespace protobuf {
}  // namespace protobuf
}  // namespace google
// @@protoc_insertion_point(global_scope)
PROTOBUF_ATTRIBUTE_INIT_PRIORITY2 static ::std::false_type
    _static_init2_ PROTOBUF_UNUSED =
        (::_pbi::AddDescriptors(&descriptor_table_llbuild3_2fCommon_2eproto),
         ::std::false_type{});
#include "google/protobuf/port_undef.inc"
