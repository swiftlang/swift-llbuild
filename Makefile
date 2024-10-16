# Helper makefile for generating llbuild3 protobuf files

.PHONY: generate
generate: generate-protos

.PHONY: generate-protos
generate-protos: proto-toolchain
	find src -name \*.pb.\* | xargs rm
	utils/tools/bin/protoc -I=src \
		--cpp_out=src \
		$$(find src/ -name \*.proto)

	utils/tools/bin/protoc -I=src \
		--plugin=utils/tools/bin/protoc-gen-swift \
		--swift_out=src \
		--swift_opt=Visibility=Public \
		--swift_opt=ProtoPathModuleMappings=src/module_map.asciipb \
		$$(find src -name \*.proto -not -name \*Internal\*)

.PHONY: proto-toolchain
proto-toolchain:
	utils/build_proto_toolchain.sh

