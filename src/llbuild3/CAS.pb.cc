// Generated by the protocol buffer compiler.  DO NOT EDIT!
// NO CHECKED-IN PROTOBUF GENCODE
// source: llbuild3/CAS.proto
// Protobuf C++ Version: 5.27.2

#include "llbuild3/CAS.pb.h"

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

inline constexpr CASID::Impl_::Impl_(
    ::_pbi::ConstantInitialized) noexcept
      : bytes_(
            &::google::protobuf::internal::fixed_address_empty_string,
            ::_pbi::ConstantInitialized()),
        _cached_size_{0} {}

template <typename>
PROTOBUF_CONSTEXPR CASID::CASID(::_pbi::ConstantInitialized)
    : _impl_(::_pbi::ConstantInitialized()) {}
struct CASIDDefaultTypeInternal {
  PROTOBUF_CONSTEXPR CASIDDefaultTypeInternal() : _instance(::_pbi::ConstantInitialized{}) {}
  ~CASIDDefaultTypeInternal() {}
  union {
    CASID _instance;
  };
};

PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT
    PROTOBUF_ATTRIBUTE_INIT_PRIORITY1 CASIDDefaultTypeInternal _CASID_default_instance_;

inline constexpr CASObject::Impl_::Impl_(
    ::_pbi::ConstantInitialized) noexcept
      : refs_{},
        data_(
            &::google::protobuf::internal::fixed_address_empty_string,
            ::_pbi::ConstantInitialized()),
        _cached_size_{0} {}

template <typename>
PROTOBUF_CONSTEXPR CASObject::CASObject(::_pbi::ConstantInitialized)
    : _impl_(::_pbi::ConstantInitialized()) {}
struct CASObjectDefaultTypeInternal {
  PROTOBUF_CONSTEXPR CASObjectDefaultTypeInternal() : _instance(::_pbi::ConstantInitialized{}) {}
  ~CASObjectDefaultTypeInternal() {}
  union {
    CASObject _instance;
  };
};

PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT
    PROTOBUF_ATTRIBUTE_INIT_PRIORITY1 CASObjectDefaultTypeInternal _CASObject_default_instance_;
}  // namespace llbuild3
static constexpr const ::_pb::EnumDescriptor**
    file_level_enum_descriptors_llbuild3_2fCAS_2eproto = nullptr;
static constexpr const ::_pb::ServiceDescriptor**
    file_level_service_descriptors_llbuild3_2fCAS_2eproto = nullptr;
const ::uint32_t
    TableStruct_llbuild3_2fCAS_2eproto::offsets[] ABSL_ATTRIBUTE_SECTION_VARIABLE(
        protodesc_cold) = {
        ~0u,  // no _has_bits_
        PROTOBUF_FIELD_OFFSET(::llbuild3::CASID, _internal_metadata_),
        ~0u,  // no _extensions_
        ~0u,  // no _oneof_case_
        ~0u,  // no _weak_field_map_
        ~0u,  // no _inlined_string_donated_
        ~0u,  // no _split_
        ~0u,  // no sizeof(Split)
        PROTOBUF_FIELD_OFFSET(::llbuild3::CASID, _impl_.bytes_),
        ~0u,  // no _has_bits_
        PROTOBUF_FIELD_OFFSET(::llbuild3::CASObject, _internal_metadata_),
        ~0u,  // no _extensions_
        ~0u,  // no _oneof_case_
        ~0u,  // no _weak_field_map_
        ~0u,  // no _inlined_string_donated_
        ~0u,  // no _split_
        ~0u,  // no sizeof(Split)
        PROTOBUF_FIELD_OFFSET(::llbuild3::CASObject, _impl_.refs_),
        PROTOBUF_FIELD_OFFSET(::llbuild3::CASObject, _impl_.data_),
};

static const ::_pbi::MigrationSchema
    schemas[] ABSL_ATTRIBUTE_SECTION_VARIABLE(protodesc_cold) = {
        {0, -1, -1, sizeof(::llbuild3::CASID)},
        {9, -1, -1, sizeof(::llbuild3::CASObject)},
};
static const ::_pb::Message* const file_default_instances[] = {
    &::llbuild3::_CASID_default_instance_._instance,
    &::llbuild3::_CASObject_default_instance_._instance,
};
const char descriptor_table_protodef_llbuild3_2fCAS_2eproto[] ABSL_ATTRIBUTE_SECTION_VARIABLE(
    protodesc_cold) = {
    "\n\022llbuild3/CAS.proto\022\010llbuild3\"\026\n\005CASID\022"
    "\r\n\005bytes\030\001 \001(\014\"8\n\tCASObject\022\035\n\004refs\030\001 \003("
    "\0132\017.llbuild3.CASID\022\014\n\004data\030\002 \001(\014b\006proto3"
};
static ::absl::once_flag descriptor_table_llbuild3_2fCAS_2eproto_once;
PROTOBUF_CONSTINIT const ::_pbi::DescriptorTable descriptor_table_llbuild3_2fCAS_2eproto = {
    false,
    false,
    120,
    descriptor_table_protodef_llbuild3_2fCAS_2eproto,
    "llbuild3/CAS.proto",
    &descriptor_table_llbuild3_2fCAS_2eproto_once,
    nullptr,
    0,
    2,
    schemas,
    file_default_instances,
    TableStruct_llbuild3_2fCAS_2eproto::offsets,
    file_level_enum_descriptors_llbuild3_2fCAS_2eproto,
    file_level_service_descriptors_llbuild3_2fCAS_2eproto,
};
namespace llbuild3 {
// ===================================================================

class CASID::_Internal {
 public:
};

CASID::CASID(::google::protobuf::Arena* arena)
    : ::google::protobuf::Message(arena) {
  SharedCtor(arena);
  // @@protoc_insertion_point(arena_constructor:llbuild3.CASID)
}
inline PROTOBUF_NDEBUG_INLINE CASID::Impl_::Impl_(
    ::google::protobuf::internal::InternalVisibility visibility, ::google::protobuf::Arena* arena,
    const Impl_& from, const ::llbuild3::CASID& from_msg)
      : bytes_(arena, from.bytes_),
        _cached_size_{0} {}

CASID::CASID(
    ::google::protobuf::Arena* arena,
    const CASID& from)
    : ::google::protobuf::Message(arena) {
  CASID* const _this = this;
  (void)_this;
  _internal_metadata_.MergeFrom<::google::protobuf::UnknownFieldSet>(
      from._internal_metadata_);
  new (&_impl_) Impl_(internal_visibility(), arena, from._impl_, from);

  // @@protoc_insertion_point(copy_constructor:llbuild3.CASID)
}
inline PROTOBUF_NDEBUG_INLINE CASID::Impl_::Impl_(
    ::google::protobuf::internal::InternalVisibility visibility,
    ::google::protobuf::Arena* arena)
      : bytes_(arena),
        _cached_size_{0} {}

inline void CASID::SharedCtor(::_pb::Arena* arena) {
  new (&_impl_) Impl_(internal_visibility(), arena);
}
CASID::~CASID() {
  // @@protoc_insertion_point(destructor:llbuild3.CASID)
  _internal_metadata_.Delete<::google::protobuf::UnknownFieldSet>();
  SharedDtor();
}
inline void CASID::SharedDtor() {
  ABSL_DCHECK(GetArena() == nullptr);
  _impl_.bytes_.Destroy();
  _impl_.~Impl_();
}

const ::google::protobuf::MessageLite::ClassData*
CASID::GetClassData() const {
  PROTOBUF_CONSTINIT static const ::google::protobuf::MessageLite::
      ClassDataFull _data_ = {
          {
              &_table_.header,
              nullptr,  // OnDemandRegisterArenaDtor
              nullptr,  // IsInitialized
              PROTOBUF_FIELD_OFFSET(CASID, _impl_._cached_size_),
              false,
          },
          &CASID::MergeImpl,
          &CASID::kDescriptorMethods,
          &descriptor_table_llbuild3_2fCAS_2eproto,
          nullptr,  // tracker
      };
  ::google::protobuf::internal::PrefetchToLocalCache(&_data_);
  ::google::protobuf::internal::PrefetchToLocalCache(_data_.tc_table);
  return _data_.base();
}
PROTOBUF_CONSTINIT PROTOBUF_ATTRIBUTE_INIT_PRIORITY1
const ::_pbi::TcParseTable<0, 1, 0, 0, 2> CASID::_table_ = {
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
    &_CASID_default_instance_._instance,
    nullptr,  // post_loop_handler
    ::_pbi::TcParser::GenericFallback,  // fallback
    #ifdef PROTOBUF_PREFETCH_PARSE_TABLE
    ::_pbi::TcParser::GetTable<::llbuild3::CASID>(),  // to_prefetch
    #endif  // PROTOBUF_PREFETCH_PARSE_TABLE
  }, {{
    // bytes bytes = 1;
    {::_pbi::TcParser::FastBS1,
     {10, 63, 0, PROTOBUF_FIELD_OFFSET(CASID, _impl_.bytes_)}},
  }}, {{
    65535, 65535
  }}, {{
    // bytes bytes = 1;
    {PROTOBUF_FIELD_OFFSET(CASID, _impl_.bytes_), 0, 0,
    (0 | ::_fl::kFcSingular | ::_fl::kBytes | ::_fl::kRepAString)},
  }},
  // no aux_entries
  {{
  }},
};

PROTOBUF_NOINLINE void CASID::Clear() {
// @@protoc_insertion_point(message_clear_start:llbuild3.CASID)
  ::google::protobuf::internal::TSanWrite(&_impl_);
  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  _impl_.bytes_.ClearToEmpty();
  _internal_metadata_.Clear<::google::protobuf::UnknownFieldSet>();
}

::uint8_t* CASID::_InternalSerialize(
    ::uint8_t* target,
    ::google::protobuf::io::EpsCopyOutputStream* stream) const {
  // @@protoc_insertion_point(serialize_to_array_start:llbuild3.CASID)
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
  // @@protoc_insertion_point(serialize_to_array_end:llbuild3.CASID)
  return target;
}

::size_t CASID::ByteSizeLong() const {
// @@protoc_insertion_point(message_byte_size_start:llbuild3.CASID)
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


void CASID::MergeImpl(::google::protobuf::MessageLite& to_msg, const ::google::protobuf::MessageLite& from_msg) {
  auto* const _this = static_cast<CASID*>(&to_msg);
  auto& from = static_cast<const CASID&>(from_msg);
  // @@protoc_insertion_point(class_specific_merge_from_start:llbuild3.CASID)
  ABSL_DCHECK_NE(&from, _this);
  ::uint32_t cached_has_bits = 0;
  (void) cached_has_bits;

  if (!from._internal_bytes().empty()) {
    _this->_internal_set_bytes(from._internal_bytes());
  }
  _this->_internal_metadata_.MergeFrom<::google::protobuf::UnknownFieldSet>(from._internal_metadata_);
}

void CASID::CopyFrom(const CASID& from) {
// @@protoc_insertion_point(class_specific_copy_from_start:llbuild3.CASID)
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}


void CASID::InternalSwap(CASID* PROTOBUF_RESTRICT other) {
  using std::swap;
  auto* arena = GetArena();
  ABSL_DCHECK_EQ(arena, other->GetArena());
  _internal_metadata_.InternalSwap(&other->_internal_metadata_);
  ::_pbi::ArenaStringPtr::InternalSwap(&_impl_.bytes_, &other->_impl_.bytes_, arena);
}

::google::protobuf::Metadata CASID::GetMetadata() const {
  return ::google::protobuf::Message::GetMetadataImpl(GetClassData()->full());
}
// ===================================================================

class CASObject::_Internal {
 public:
};

CASObject::CASObject(::google::protobuf::Arena* arena)
    : ::google::protobuf::Message(arena) {
  SharedCtor(arena);
  // @@protoc_insertion_point(arena_constructor:llbuild3.CASObject)
}
inline PROTOBUF_NDEBUG_INLINE CASObject::Impl_::Impl_(
    ::google::protobuf::internal::InternalVisibility visibility, ::google::protobuf::Arena* arena,
    const Impl_& from, const ::llbuild3::CASObject& from_msg)
      : refs_{visibility, arena, from.refs_},
        data_(arena, from.data_),
        _cached_size_{0} {}

CASObject::CASObject(
    ::google::protobuf::Arena* arena,
    const CASObject& from)
    : ::google::protobuf::Message(arena) {
  CASObject* const _this = this;
  (void)_this;
  _internal_metadata_.MergeFrom<::google::protobuf::UnknownFieldSet>(
      from._internal_metadata_);
  new (&_impl_) Impl_(internal_visibility(), arena, from._impl_, from);

  // @@protoc_insertion_point(copy_constructor:llbuild3.CASObject)
}
inline PROTOBUF_NDEBUG_INLINE CASObject::Impl_::Impl_(
    ::google::protobuf::internal::InternalVisibility visibility,
    ::google::protobuf::Arena* arena)
      : refs_{visibility, arena},
        data_(arena),
        _cached_size_{0} {}

inline void CASObject::SharedCtor(::_pb::Arena* arena) {
  new (&_impl_) Impl_(internal_visibility(), arena);
}
CASObject::~CASObject() {
  // @@protoc_insertion_point(destructor:llbuild3.CASObject)
  _internal_metadata_.Delete<::google::protobuf::UnknownFieldSet>();
  SharedDtor();
}
inline void CASObject::SharedDtor() {
  ABSL_DCHECK(GetArena() == nullptr);
  _impl_.data_.Destroy();
  _impl_.~Impl_();
}

const ::google::protobuf::MessageLite::ClassData*
CASObject::GetClassData() const {
  PROTOBUF_CONSTINIT static const ::google::protobuf::MessageLite::
      ClassDataFull _data_ = {
          {
              &_table_.header,
              nullptr,  // OnDemandRegisterArenaDtor
              nullptr,  // IsInitialized
              PROTOBUF_FIELD_OFFSET(CASObject, _impl_._cached_size_),
              false,
          },
          &CASObject::MergeImpl,
          &CASObject::kDescriptorMethods,
          &descriptor_table_llbuild3_2fCAS_2eproto,
          nullptr,  // tracker
      };
  ::google::protobuf::internal::PrefetchToLocalCache(&_data_);
  ::google::protobuf::internal::PrefetchToLocalCache(_data_.tc_table);
  return _data_.base();
}
PROTOBUF_CONSTINIT PROTOBUF_ATTRIBUTE_INIT_PRIORITY1
const ::_pbi::TcParseTable<1, 2, 1, 0, 2> CASObject::_table_ = {
  {
    0,  // no _has_bits_
    0, // no _extensions_
    2, 8,  // max_field_number, fast_idx_mask
    offsetof(decltype(_table_), field_lookup_table),
    4294967292,  // skipmap
    offsetof(decltype(_table_), field_entries),
    2,  // num_field_entries
    1,  // num_aux_entries
    offsetof(decltype(_table_), aux_entries),
    &_CASObject_default_instance_._instance,
    nullptr,  // post_loop_handler
    ::_pbi::TcParser::GenericFallback,  // fallback
    #ifdef PROTOBUF_PREFETCH_PARSE_TABLE
    ::_pbi::TcParser::GetTable<::llbuild3::CASObject>(),  // to_prefetch
    #endif  // PROTOBUF_PREFETCH_PARSE_TABLE
  }, {{
    // bytes data = 2;
    {::_pbi::TcParser::FastBS1,
     {18, 63, 0, PROTOBUF_FIELD_OFFSET(CASObject, _impl_.data_)}},
    // repeated .llbuild3.CASID refs = 1;
    {::_pbi::TcParser::FastMtR1,
     {10, 63, 0, PROTOBUF_FIELD_OFFSET(CASObject, _impl_.refs_)}},
  }}, {{
    65535, 65535
  }}, {{
    // repeated .llbuild3.CASID refs = 1;
    {PROTOBUF_FIELD_OFFSET(CASObject, _impl_.refs_), 0, 0,
    (0 | ::_fl::kFcRepeated | ::_fl::kMessage | ::_fl::kTvTable)},
    // bytes data = 2;
    {PROTOBUF_FIELD_OFFSET(CASObject, _impl_.data_), 0, 0,
    (0 | ::_fl::kFcSingular | ::_fl::kBytes | ::_fl::kRepAString)},
  }}, {{
    {::_pbi::TcParser::GetTable<::llbuild3::CASID>()},
  }}, {{
  }},
};

PROTOBUF_NOINLINE void CASObject::Clear() {
// @@protoc_insertion_point(message_clear_start:llbuild3.CASObject)
  ::google::protobuf::internal::TSanWrite(&_impl_);
  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  _impl_.refs_.Clear();
  _impl_.data_.ClearToEmpty();
  _internal_metadata_.Clear<::google::protobuf::UnknownFieldSet>();
}

::uint8_t* CASObject::_InternalSerialize(
    ::uint8_t* target,
    ::google::protobuf::io::EpsCopyOutputStream* stream) const {
  // @@protoc_insertion_point(serialize_to_array_start:llbuild3.CASObject)
  ::uint32_t cached_has_bits = 0;
  (void)cached_has_bits;

  // repeated .llbuild3.CASID refs = 1;
  for (unsigned i = 0, n = static_cast<unsigned>(
                           this->_internal_refs_size());
       i < n; i++) {
    const auto& repfield = this->_internal_refs().Get(i);
    target =
        ::google::protobuf::internal::WireFormatLite::InternalWriteMessage(
            1, repfield, repfield.GetCachedSize(),
            target, stream);
  }

  // bytes data = 2;
  if (!this->_internal_data().empty()) {
    const std::string& _s = this->_internal_data();
    target = stream->WriteBytesMaybeAliased(2, _s, target);
  }

  if (PROTOBUF_PREDICT_FALSE(_internal_metadata_.have_unknown_fields())) {
    target =
        ::_pbi::WireFormat::InternalSerializeUnknownFieldsToArray(
            _internal_metadata_.unknown_fields<::google::protobuf::UnknownFieldSet>(::google::protobuf::UnknownFieldSet::default_instance), target, stream);
  }
  // @@protoc_insertion_point(serialize_to_array_end:llbuild3.CASObject)
  return target;
}

::size_t CASObject::ByteSizeLong() const {
// @@protoc_insertion_point(message_byte_size_start:llbuild3.CASObject)
  ::size_t total_size = 0;

  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  ::_pbi::Prefetch5LinesFrom7Lines(reinterpret_cast<const void*>(this));
  // repeated .llbuild3.CASID refs = 1;
  total_size += 1UL * this->_internal_refs_size();
  for (const auto& msg : this->_internal_refs()) {
    total_size += ::google::protobuf::internal::WireFormatLite::MessageSize(msg);
  }
  // bytes data = 2;
  if (!this->_internal_data().empty()) {
    total_size += 1 + ::google::protobuf::internal::WireFormatLite::BytesSize(
                                    this->_internal_data());
  }

  return MaybeComputeUnknownFieldsSize(total_size, &_impl_._cached_size_);
}


void CASObject::MergeImpl(::google::protobuf::MessageLite& to_msg, const ::google::protobuf::MessageLite& from_msg) {
  auto* const _this = static_cast<CASObject*>(&to_msg);
  auto& from = static_cast<const CASObject&>(from_msg);
  // @@protoc_insertion_point(class_specific_merge_from_start:llbuild3.CASObject)
  ABSL_DCHECK_NE(&from, _this);
  ::uint32_t cached_has_bits = 0;
  (void) cached_has_bits;

  _this->_internal_mutable_refs()->MergeFrom(
      from._internal_refs());
  if (!from._internal_data().empty()) {
    _this->_internal_set_data(from._internal_data());
  }
  _this->_internal_metadata_.MergeFrom<::google::protobuf::UnknownFieldSet>(from._internal_metadata_);
}

void CASObject::CopyFrom(const CASObject& from) {
// @@protoc_insertion_point(class_specific_copy_from_start:llbuild3.CASObject)
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}


void CASObject::InternalSwap(CASObject* PROTOBUF_RESTRICT other) {
  using std::swap;
  auto* arena = GetArena();
  ABSL_DCHECK_EQ(arena, other->GetArena());
  _internal_metadata_.InternalSwap(&other->_internal_metadata_);
  _impl_.refs_.InternalSwap(&other->_impl_.refs_);
  ::_pbi::ArenaStringPtr::InternalSwap(&_impl_.data_, &other->_impl_.data_, arena);
}

::google::protobuf::Metadata CASObject::GetMetadata() const {
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
        (::_pbi::AddDescriptors(&descriptor_table_llbuild3_2fCAS_2eproto),
         ::std::false_type{});
#include "google/protobuf/port_undef.inc"
