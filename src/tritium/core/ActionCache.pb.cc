// Generated by the protocol buffer compiler.  DO NOT EDIT!
// NO CHECKED-IN PROTOBUF GENCODE
// source: tritium/core/ActionCache.proto
// Protobuf C++ Version: 5.27.2

#include "tritium/core/ActionCache.pb.h"

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
}  // namespace core
}  // namespace tritium
static const ::_pb::EnumDescriptor* file_level_enum_descriptors_tritium_2fcore_2fActionCache_2eproto[1];
static constexpr const ::_pb::ServiceDescriptor**
    file_level_service_descriptors_tritium_2fcore_2fActionCache_2eproto = nullptr;
const ::uint32_t TableStruct_tritium_2fcore_2fActionCache_2eproto::offsets[1] = {};
static constexpr ::_pbi::MigrationSchema* schemas = nullptr;
static constexpr ::_pb::Message* const* file_default_instances = nullptr;
const char descriptor_table_protodef_tritium_2fcore_2fActionCache_2eproto[] ABSL_ATTRIBUTE_SECTION_VARIABLE(
    protodesc_cold) = {
    "\n\036tritium/core/ActionCache.proto\022\014tritiu"
    "m.core*)\n\007KeyType\022\010\n\004RULE\020\000\022\010\n\004TASK\020\001\022\n\n"
    "\006ACTION\020\002b\006proto3"
};
static ::absl::once_flag descriptor_table_tritium_2fcore_2fActionCache_2eproto_once;
PROTOBUF_CONSTINIT const ::_pbi::DescriptorTable descriptor_table_tritium_2fcore_2fActionCache_2eproto = {
    false,
    false,
    97,
    descriptor_table_protodef_tritium_2fcore_2fActionCache_2eproto,
    "tritium/core/ActionCache.proto",
    &descriptor_table_tritium_2fcore_2fActionCache_2eproto_once,
    nullptr,
    0,
    0,
    schemas,
    file_default_instances,
    TableStruct_tritium_2fcore_2fActionCache_2eproto::offsets,
    file_level_enum_descriptors_tritium_2fcore_2fActionCache_2eproto,
    file_level_service_descriptors_tritium_2fcore_2fActionCache_2eproto,
};
namespace tritium {
namespace core {
const ::google::protobuf::EnumDescriptor* KeyType_descriptor() {
  ::google::protobuf::internal::AssignDescriptors(&descriptor_table_tritium_2fcore_2fActionCache_2eproto);
  return file_level_enum_descriptors_tritium_2fcore_2fActionCache_2eproto[0];
}
PROTOBUF_CONSTINIT const uint32_t KeyType_internal_data_[] = {
    196608u, 0u, };
bool KeyType_IsValid(int value) {
  return 0 <= value && value <= 2;
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
        (::_pbi::AddDescriptors(&descriptor_table_tritium_2fcore_2fActionCache_2eproto),
         ::std::false_type{});
#include "google/protobuf/port_undef.inc"
