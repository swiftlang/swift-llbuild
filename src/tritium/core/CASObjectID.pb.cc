// Generated by the protocol buffer compiler.  DO NOT EDIT!
// NO CHECKED-IN PROTOBUF GENCODE
// source: tritium/core/CASObjectID.proto
// Protobuf C++ Version: 5.27.2

#include "tritium/core/CASObjectID.pb.h"

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
namespace core {

inline constexpr CASObjectID::Impl_::Impl_(
    ::_pbi::ConstantInitialized) noexcept
      : bytes_(
            &::google::protobuf::internal::fixed_address_empty_string,
            ::_pbi::ConstantInitialized()),
        _cached_size_{0} {}

template <typename>
PROTOBUF_CONSTEXPR CASObjectID::CASObjectID(::_pbi::ConstantInitialized)
    : _impl_(::_pbi::ConstantInitialized()) {}
struct CASObjectIDDefaultTypeInternal {
  PROTOBUF_CONSTEXPR CASObjectIDDefaultTypeInternal() : _instance(::_pbi::ConstantInitialized{}) {}
  ~CASObjectIDDefaultTypeInternal() {}
  union {
    CASObjectID _instance;
  };
};

PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT
    PROTOBUF_ATTRIBUTE_INIT_PRIORITY1 CASObjectIDDefaultTypeInternal _CASObjectID_default_instance_;
}  // namespace core
}  // namespace tritium
static constexpr const ::_pb::EnumDescriptor**
    file_level_enum_descriptors_tritium_2fcore_2fCASObjectID_2eproto = nullptr;
static constexpr const ::_pb::ServiceDescriptor**
    file_level_service_descriptors_tritium_2fcore_2fCASObjectID_2eproto = nullptr;
const ::uint32_t
    TableStruct_tritium_2fcore_2fCASObjectID_2eproto::offsets[] ABSL_ATTRIBUTE_SECTION_VARIABLE(
        protodesc_cold) = {
        ~0u,  // no _has_bits_
        PROTOBUF_FIELD_OFFSET(::tritium::core::CASObjectID, _internal_metadata_),
        ~0u,  // no _extensions_
        ~0u,  // no _oneof_case_
        ~0u,  // no _weak_field_map_
        ~0u,  // no _inlined_string_donated_
        ~0u,  // no _split_
        ~0u,  // no sizeof(Split)
        PROTOBUF_FIELD_OFFSET(::tritium::core::CASObjectID, _impl_.bytes_),
};

static const ::_pbi::MigrationSchema
    schemas[] ABSL_ATTRIBUTE_SECTION_VARIABLE(protodesc_cold) = {
        {0, -1, -1, sizeof(::tritium::core::CASObjectID)},
};
static const ::_pb::Message* const file_default_instances[] = {
    &::tritium::core::_CASObjectID_default_instance_._instance,
};
const char descriptor_table_protodef_tritium_2fcore_2fCASObjectID_2eproto[] ABSL_ATTRIBUTE_SECTION_VARIABLE(
    protodesc_cold) = {
    "\n\036tritium/core/CASObjectID.proto\022\014tritiu"
    "m.core\"\034\n\013CASObjectID\022\r\n\005bytes\030\001 \001(\014b\006pr"
    "oto3"
};
static ::absl::once_flag descriptor_table_tritium_2fcore_2fCASObjectID_2eproto_once;
PROTOBUF_CONSTINIT const ::_pbi::DescriptorTable descriptor_table_tritium_2fcore_2fCASObjectID_2eproto = {
    false,
    false,
    84,
    descriptor_table_protodef_tritium_2fcore_2fCASObjectID_2eproto,
    "tritium/core/CASObjectID.proto",
    &descriptor_table_tritium_2fcore_2fCASObjectID_2eproto_once,
    nullptr,
    0,
    1,
    schemas,
    file_default_instances,
    TableStruct_tritium_2fcore_2fCASObjectID_2eproto::offsets,
    file_level_enum_descriptors_tritium_2fcore_2fCASObjectID_2eproto,
    file_level_service_descriptors_tritium_2fcore_2fCASObjectID_2eproto,
};
namespace tritium {
namespace core {
// ===================================================================

class CASObjectID::_Internal {
 public:
};

CASObjectID::CASObjectID(::google::protobuf::Arena* arena)
    : ::google::protobuf::Message(arena) {
  SharedCtor(arena);
  // @@protoc_insertion_point(arena_constructor:tritium.core.CASObjectID)
}
inline PROTOBUF_NDEBUG_INLINE CASObjectID::Impl_::Impl_(
    ::google::protobuf::internal::InternalVisibility visibility, ::google::protobuf::Arena* arena,
    const Impl_& from, const ::tritium::core::CASObjectID& from_msg)
      : bytes_(arena, from.bytes_),
        _cached_size_{0} {}

CASObjectID::CASObjectID(
    ::google::protobuf::Arena* arena,
    const CASObjectID& from)
    : ::google::protobuf::Message(arena) {
  CASObjectID* const _this = this;
  (void)_this;
  _internal_metadata_.MergeFrom<::google::protobuf::UnknownFieldSet>(
      from._internal_metadata_);
  new (&_impl_) Impl_(internal_visibility(), arena, from._impl_, from);

  // @@protoc_insertion_point(copy_constructor:tritium.core.CASObjectID)
}
inline PROTOBUF_NDEBUG_INLINE CASObjectID::Impl_::Impl_(
    ::google::protobuf::internal::InternalVisibility visibility,
    ::google::protobuf::Arena* arena)
      : bytes_(arena),
        _cached_size_{0} {}

inline void CASObjectID::SharedCtor(::_pb::Arena* arena) {
  new (&_impl_) Impl_(internal_visibility(), arena);
}
CASObjectID::~CASObjectID() {
  // @@protoc_insertion_point(destructor:tritium.core.CASObjectID)
  _internal_metadata_.Delete<::google::protobuf::UnknownFieldSet>();
  SharedDtor();
}
inline void CASObjectID::SharedDtor() {
  ABSL_DCHECK(GetArena() == nullptr);
  _impl_.bytes_.Destroy();
  _impl_.~Impl_();
}

const ::google::protobuf::MessageLite::ClassData*
CASObjectID::GetClassData() const {
  PROTOBUF_CONSTINIT static const ::google::protobuf::MessageLite::
      ClassDataFull _data_ = {
          {
              &_table_.header,
              nullptr,  // OnDemandRegisterArenaDtor
              nullptr,  // IsInitialized
              PROTOBUF_FIELD_OFFSET(CASObjectID, _impl_._cached_size_),
              false,
          },
          &CASObjectID::MergeImpl,
          &CASObjectID::kDescriptorMethods,
          &descriptor_table_tritium_2fcore_2fCASObjectID_2eproto,
          nullptr,  // tracker
      };
  ::google::protobuf::internal::PrefetchToLocalCache(&_data_);
  ::google::protobuf::internal::PrefetchToLocalCache(_data_.tc_table);
  return _data_.base();
}
PROTOBUF_CONSTINIT PROTOBUF_ATTRIBUTE_INIT_PRIORITY1
const ::_pbi::TcParseTable<0, 1, 0, 0, 2> CASObjectID::_table_ = {
  {
    0,  // no _has_bits_
    0, // no _extensions_
    1, 0,  // max_field_number, fast_idx_mask
    offsetof(decltype(_table_), field_lookup_table),
    4294967294,  // skipmap
    offsetof(decltype(_table_), field_entries),
    1,  // num_field_entries
    0,  // num_aux_entries
    offsetof(decltype(_table_), field_names),  // no aux_entries
    &_CASObjectID_default_instance_._instance,
    nullptr,  // post_loop_handler
    ::_pbi::TcParser::GenericFallback,  // fallback
    #ifdef PROTOBUF_PREFETCH_PARSE_TABLE
    ::_pbi::TcParser::GetTable<::tritium::core::CASObjectID>(),  // to_prefetch
    #endif  // PROTOBUF_PREFETCH_PARSE_TABLE
  }, {{
    // bytes bytes = 1;
    {::_pbi::TcParser::FastBS1,
     {10, 63, 0, PROTOBUF_FIELD_OFFSET(CASObjectID, _impl_.bytes_)}},
  }}, {{
    65535, 65535
  }}, {{
    // bytes bytes = 1;
    {PROTOBUF_FIELD_OFFSET(CASObjectID, _impl_.bytes_), 0, 0,
    (0 | ::_fl::kFcSingular | ::_fl::kBytes | ::_fl::kRepAString)},
  }},
  // no aux_entries
  {{
  }},
};

PROTOBUF_NOINLINE void CASObjectID::Clear() {
// @@protoc_insertion_point(message_clear_start:tritium.core.CASObjectID)
  ::google::protobuf::internal::TSanWrite(&_impl_);
  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  _impl_.bytes_.ClearToEmpty();
  _internal_metadata_.Clear<::google::protobuf::UnknownFieldSet>();
}

::uint8_t* CASObjectID::_InternalSerialize(
    ::uint8_t* target,
    ::google::protobuf::io::EpsCopyOutputStream* stream) const {
  // @@protoc_insertion_point(serialize_to_array_start:tritium.core.CASObjectID)
  ::uint32_t cached_has_bits = 0;
  (void)cached_has_bits;

  // bytes bytes = 1;
  if (!this->_internal_bytes().empty()) {
    const std::string& _s = this->_internal_bytes();
    target = stream->WriteBytesMaybeAliased(1, _s, target);
  }

  if (PROTOBUF_PREDICT_FALSE(_internal_metadata_.have_unknown_fields())) {
    target =
        ::_pbi::WireFormat::InternalSerializeUnknownFieldsToArray(
            _internal_metadata_.unknown_fields<::google::protobuf::UnknownFieldSet>(::google::protobuf::UnknownFieldSet::default_instance), target, stream);
  }
  // @@protoc_insertion_point(serialize_to_array_end:tritium.core.CASObjectID)
  return target;
}

::size_t CASObjectID::ByteSizeLong() const {
// @@protoc_insertion_point(message_byte_size_start:tritium.core.CASObjectID)
  ::size_t total_size = 0;

  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  // bytes bytes = 1;
  if (!this->_internal_bytes().empty()) {
    total_size += 1 + ::google::protobuf::internal::WireFormatLite::BytesSize(
                                    this->_internal_bytes());
  }

  return MaybeComputeUnknownFieldsSize(total_size, &_impl_._cached_size_);
}


void CASObjectID::MergeImpl(::google::protobuf::MessageLite& to_msg, const ::google::protobuf::MessageLite& from_msg) {
  auto* const _this = static_cast<CASObjectID*>(&to_msg);
  auto& from = static_cast<const CASObjectID&>(from_msg);
  // @@protoc_insertion_point(class_specific_merge_from_start:tritium.core.CASObjectID)
  ABSL_DCHECK_NE(&from, _this);
  ::uint32_t cached_has_bits = 0;
  (void) cached_has_bits;

  if (!from._internal_bytes().empty()) {
    _this->_internal_set_bytes(from._internal_bytes());
  }
  _this->_internal_metadata_.MergeFrom<::google::protobuf::UnknownFieldSet>(from._internal_metadata_);
}

void CASObjectID::CopyFrom(const CASObjectID& from) {
// @@protoc_insertion_point(class_specific_copy_from_start:tritium.core.CASObjectID)
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}


void CASObjectID::InternalSwap(CASObjectID* PROTOBUF_RESTRICT other) {
  using std::swap;
  auto* arena = GetArena();
  ABSL_DCHECK_EQ(arena, other->GetArena());
  _internal_metadata_.InternalSwap(&other->_internal_metadata_);
  ::_pbi::ArenaStringPtr::InternalSwap(&_impl_.bytes_, &other->_impl_.bytes_, arena);
}

::google::protobuf::Metadata CASObjectID::GetMetadata() const {
  return ::google::protobuf::Message::GetMetadataImpl(GetClassData()->full());
}
// @@protoc_insertion_point(namespace_scope)
}  // namespace core
}  // namespace tritium
namespace google {
namespace protobuf {
}  // namespace protobuf
}  // namespace google
// @@protoc_insertion_point(global_scope)
PROTOBUF_ATTRIBUTE_INIT_PRIORITY2 static ::std::false_type
    _static_init2_ PROTOBUF_UNUSED =
        (::_pbi::AddDescriptors(&descriptor_table_tritium_2fcore_2fCASObjectID_2eproto),
         ::std::false_type{});
#include "google/protobuf/port_undef.inc"
