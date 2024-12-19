// Generated by the protocol buffer compiler.  DO NOT EDIT!
// NO CHECKED-IN PROTOBUF GENCODE
// source: llbuild3/core/Label.proto
// Protobuf C++ Version: 5.27.2

#include "llbuild3/core/Label.pb.h"

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
namespace core {

inline constexpr Label::Impl_::Impl_(
    ::_pbi::ConstantInitialized) noexcept
      : components_{},
        name_(
            &::google::protobuf::internal::fixed_address_empty_string,
            ::_pbi::ConstantInitialized()),
        _cached_size_{0} {}

template <typename>
PROTOBUF_CONSTEXPR Label::Label(::_pbi::ConstantInitialized)
    : _impl_(::_pbi::ConstantInitialized()) {}
struct LabelDefaultTypeInternal {
  PROTOBUF_CONSTEXPR LabelDefaultTypeInternal() : _instance(::_pbi::ConstantInitialized{}) {}
  ~LabelDefaultTypeInternal() {}
  union {
    Label _instance;
  };
};

PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT
    PROTOBUF_ATTRIBUTE_INIT_PRIORITY1 LabelDefaultTypeInternal _Label_default_instance_;
}  // namespace core
}  // namespace llbuild3
static constexpr const ::_pb::EnumDescriptor**
    file_level_enum_descriptors_llbuild3_2fcore_2fLabel_2eproto = nullptr;
static constexpr const ::_pb::ServiceDescriptor**
    file_level_service_descriptors_llbuild3_2fcore_2fLabel_2eproto = nullptr;
const ::uint32_t
    TableStruct_llbuild3_2fcore_2fLabel_2eproto::offsets[] ABSL_ATTRIBUTE_SECTION_VARIABLE(
        protodesc_cold) = {
        ~0u,  // no _has_bits_
        PROTOBUF_FIELD_OFFSET(::llbuild3::core::Label, _internal_metadata_),
        ~0u,  // no _extensions_
        ~0u,  // no _oneof_case_
        ~0u,  // no _weak_field_map_
        ~0u,  // no _inlined_string_donated_
        ~0u,  // no _split_
        ~0u,  // no sizeof(Split)
        PROTOBUF_FIELD_OFFSET(::llbuild3::core::Label, _impl_.components_),
        PROTOBUF_FIELD_OFFSET(::llbuild3::core::Label, _impl_.name_),
};

static const ::_pbi::MigrationSchema
    schemas[] ABSL_ATTRIBUTE_SECTION_VARIABLE(protodesc_cold) = {
        {0, -1, -1, sizeof(::llbuild3::core::Label)},
};
static const ::_pb::Message* const file_default_instances[] = {
    &::llbuild3::core::_Label_default_instance_._instance,
};
const char descriptor_table_protodef_llbuild3_2fcore_2fLabel_2eproto[] ABSL_ATTRIBUTE_SECTION_VARIABLE(
    protodesc_cold) = {
    "\n\031llbuild3/core/Label.proto\022\rllbuild3.co"
    "re\")\n\005Label\022\022\n\ncomponents\030\001 \003(\t\022\014\n\004name\030"
    "\002 \001(\tb\006proto3"
};
static ::absl::once_flag descriptor_table_llbuild3_2fcore_2fLabel_2eproto_once;
PROTOBUF_CONSTINIT const ::_pbi::DescriptorTable descriptor_table_llbuild3_2fcore_2fLabel_2eproto = {
    false,
    false,
    93,
    descriptor_table_protodef_llbuild3_2fcore_2fLabel_2eproto,
    "llbuild3/core/Label.proto",
    &descriptor_table_llbuild3_2fcore_2fLabel_2eproto_once,
    nullptr,
    0,
    1,
    schemas,
    file_default_instances,
    TableStruct_llbuild3_2fcore_2fLabel_2eproto::offsets,
    file_level_enum_descriptors_llbuild3_2fcore_2fLabel_2eproto,
    file_level_service_descriptors_llbuild3_2fcore_2fLabel_2eproto,
};
namespace llbuild3 {
namespace core {
// ===================================================================

class Label::_Internal {
 public:
};

Label::Label(::google::protobuf::Arena* arena)
    : ::google::protobuf::Message(arena) {
  SharedCtor(arena);
  // @@protoc_insertion_point(arena_constructor:llbuild3.core.Label)
}
inline PROTOBUF_NDEBUG_INLINE Label::Impl_::Impl_(
    ::google::protobuf::internal::InternalVisibility visibility, ::google::protobuf::Arena* arena,
    const Impl_& from, const ::llbuild3::core::Label& from_msg)
      : components_{visibility, arena, from.components_},
        name_(arena, from.name_),
        _cached_size_{0} {}

Label::Label(
    ::google::protobuf::Arena* arena,
    const Label& from)
    : ::google::protobuf::Message(arena) {
  Label* const _this = this;
  (void)_this;
  _internal_metadata_.MergeFrom<::google::protobuf::UnknownFieldSet>(
      from._internal_metadata_);
  new (&_impl_) Impl_(internal_visibility(), arena, from._impl_, from);

  // @@protoc_insertion_point(copy_constructor:llbuild3.core.Label)
}
inline PROTOBUF_NDEBUG_INLINE Label::Impl_::Impl_(
    ::google::protobuf::internal::InternalVisibility visibility,
    ::google::protobuf::Arena* arena)
      : components_{visibility, arena},
        name_(arena),
        _cached_size_{0} {}

inline void Label::SharedCtor(::_pb::Arena* arena) {
  new (&_impl_) Impl_(internal_visibility(), arena);
}
Label::~Label() {
  // @@protoc_insertion_point(destructor:llbuild3.core.Label)
  _internal_metadata_.Delete<::google::protobuf::UnknownFieldSet>();
  SharedDtor();
}
inline void Label::SharedDtor() {
  ABSL_DCHECK(GetArena() == nullptr);
  _impl_.name_.Destroy();
  _impl_.~Impl_();
}

const ::google::protobuf::MessageLite::ClassData*
Label::GetClassData() const {
  PROTOBUF_CONSTINIT static const ::google::protobuf::MessageLite::
      ClassDataFull _data_ = {
          {
              &_table_.header,
              nullptr,  // OnDemandRegisterArenaDtor
              nullptr,  // IsInitialized
              PROTOBUF_FIELD_OFFSET(Label, _impl_._cached_size_),
              false,
          },
          &Label::MergeImpl,
          &Label::kDescriptorMethods,
          &descriptor_table_llbuild3_2fcore_2fLabel_2eproto,
          nullptr,  // tracker
      };
  ::google::protobuf::internal::PrefetchToLocalCache(&_data_);
  ::google::protobuf::internal::PrefetchToLocalCache(_data_.tc_table);
  return _data_.base();
}
PROTOBUF_CONSTINIT PROTOBUF_ATTRIBUTE_INIT_PRIORITY1
const ::_pbi::TcParseTable<1, 2, 0, 42, 2> Label::_table_ = {
  {
    0,  // no _has_bits_
    0, // no _extensions_
    2, 8,  // max_field_number, fast_idx_mask
    offsetof(decltype(_table_), field_lookup_table),
    4294967292,  // skipmap
    offsetof(decltype(_table_), field_entries),
    2,  // num_field_entries
    0,  // num_aux_entries
    offsetof(decltype(_table_), field_names),  // no aux_entries
    &_Label_default_instance_._instance,
    nullptr,  // post_loop_handler
    ::_pbi::TcParser::GenericFallback,  // fallback
    #ifdef PROTOBUF_PREFETCH_PARSE_TABLE
    ::_pbi::TcParser::GetTable<::llbuild3::core::Label>(),  // to_prefetch
    #endif  // PROTOBUF_PREFETCH_PARSE_TABLE
  }, {{
    // string name = 2;
    {::_pbi::TcParser::FastUS1,
     {18, 63, 0, PROTOBUF_FIELD_OFFSET(Label, _impl_.name_)}},
    // repeated string components = 1;
    {::_pbi::TcParser::FastUR1,
     {10, 63, 0, PROTOBUF_FIELD_OFFSET(Label, _impl_.components_)}},
  }}, {{
    65535, 65535
  }}, {{
    // repeated string components = 1;
    {PROTOBUF_FIELD_OFFSET(Label, _impl_.components_), 0, 0,
    (0 | ::_fl::kFcRepeated | ::_fl::kUtf8String | ::_fl::kRepSString)},
    // string name = 2;
    {PROTOBUF_FIELD_OFFSET(Label, _impl_.name_), 0, 0,
    (0 | ::_fl::kFcSingular | ::_fl::kUtf8String | ::_fl::kRepAString)},
  }},
  // no aux_entries
  {{
    "\23\12\4\0\0\0\0\0"
    "llbuild3.core.Label"
    "components"
    "name"
  }},
};

PROTOBUF_NOINLINE void Label::Clear() {
// @@protoc_insertion_point(message_clear_start:llbuild3.core.Label)
  ::google::protobuf::internal::TSanWrite(&_impl_);
  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  _impl_.components_.Clear();
  _impl_.name_.ClearToEmpty();
  _internal_metadata_.Clear<::google::protobuf::UnknownFieldSet>();
}

::uint8_t* Label::_InternalSerialize(
    ::uint8_t* target,
    ::google::protobuf::io::EpsCopyOutputStream* stream) const {
  // @@protoc_insertion_point(serialize_to_array_start:llbuild3.core.Label)
  ::uint32_t cached_has_bits = 0;
  (void)cached_has_bits;

  // repeated string components = 1;
  for (int i = 0, n = this->_internal_components_size(); i < n; ++i) {
    const auto& s = this->_internal_components().Get(i);
    ::google::protobuf::internal::WireFormatLite::VerifyUtf8String(
        s.data(), static_cast<int>(s.length()), ::google::protobuf::internal::WireFormatLite::SERIALIZE, "llbuild3.core.Label.components");
    target = stream->WriteString(1, s, target);
  }

  // string name = 2;
  if (!this->_internal_name().empty()) {
    const std::string& _s = this->_internal_name();
    ::google::protobuf::internal::WireFormatLite::VerifyUtf8String(
        _s.data(), static_cast<int>(_s.length()), ::google::protobuf::internal::WireFormatLite::SERIALIZE, "llbuild3.core.Label.name");
    target = stream->WriteStringMaybeAliased(2, _s, target);
  }

  if (PROTOBUF_PREDICT_FALSE(_internal_metadata_.have_unknown_fields())) {
    target =
        ::_pbi::WireFormat::InternalSerializeUnknownFieldsToArray(
            _internal_metadata_.unknown_fields<::google::protobuf::UnknownFieldSet>(::google::protobuf::UnknownFieldSet::default_instance), target, stream);
  }
  // @@protoc_insertion_point(serialize_to_array_end:llbuild3.core.Label)
  return target;
}

::size_t Label::ByteSizeLong() const {
// @@protoc_insertion_point(message_byte_size_start:llbuild3.core.Label)
  ::size_t total_size = 0;

  ::uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  ::_pbi::Prefetch5LinesFrom7Lines(reinterpret_cast<const void*>(this));
  // repeated string components = 1;
  total_size += 1 * ::google::protobuf::internal::FromIntSize(_internal_components().size());
  for (int i = 0, n = _internal_components().size(); i < n; ++i) {
    total_size += ::google::protobuf::internal::WireFormatLite::StringSize(
        _internal_components().Get(i));
  }
  // string name = 2;
  if (!this->_internal_name().empty()) {
    total_size += 1 + ::google::protobuf::internal::WireFormatLite::StringSize(
                                    this->_internal_name());
  }

  return MaybeComputeUnknownFieldsSize(total_size, &_impl_._cached_size_);
}


void Label::MergeImpl(::google::protobuf::MessageLite& to_msg, const ::google::protobuf::MessageLite& from_msg) {
  auto* const _this = static_cast<Label*>(&to_msg);
  auto& from = static_cast<const Label&>(from_msg);
  // @@protoc_insertion_point(class_specific_merge_from_start:llbuild3.core.Label)
  ABSL_DCHECK_NE(&from, _this);
  ::uint32_t cached_has_bits = 0;
  (void) cached_has_bits;

  _this->_internal_mutable_components()->MergeFrom(from._internal_components());
  if (!from._internal_name().empty()) {
    _this->_internal_set_name(from._internal_name());
  }
  _this->_internal_metadata_.MergeFrom<::google::protobuf::UnknownFieldSet>(from._internal_metadata_);
}

void Label::CopyFrom(const Label& from) {
// @@protoc_insertion_point(class_specific_copy_from_start:llbuild3.core.Label)
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}


void Label::InternalSwap(Label* PROTOBUF_RESTRICT other) {
  using std::swap;
  auto* arena = GetArena();
  ABSL_DCHECK_EQ(arena, other->GetArena());
  _internal_metadata_.InternalSwap(&other->_internal_metadata_);
  _impl_.components_.InternalSwap(&other->_impl_.components_);
  ::_pbi::ArenaStringPtr::InternalSwap(&_impl_.name_, &other->_impl_.name_, arena);
}

::google::protobuf::Metadata Label::GetMetadata() const {
  return ::google::protobuf::Message::GetMetadataImpl(GetClassData()->full());
}
// @@protoc_insertion_point(namespace_scope)
}  // namespace core
}  // namespace llbuild3
namespace google {
namespace protobuf {
}  // namespace protobuf
}  // namespace google
// @@protoc_insertion_point(global_scope)
PROTOBUF_ATTRIBUTE_INIT_PRIORITY2 static ::std::false_type
    _static_init2_ PROTOBUF_UNUSED =
        (::_pbi::AddDescriptors(&descriptor_table_llbuild3_2fcore_2fLabel_2eproto),
         ::std::false_type{});
#include "google/protobuf/port_undef.inc"