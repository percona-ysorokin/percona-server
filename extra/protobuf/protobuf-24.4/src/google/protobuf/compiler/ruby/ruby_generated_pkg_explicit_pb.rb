# frozen_string_literal: true
# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: ruby_generated_pkg_explicit.proto

require 'google/protobuf'


descriptor_data = "\n!ruby_generated_pkg_explicit.proto\x12\x0fone.two.a_three\"\x18\n\x04\x46our\x12\x10\n\x08\x61_string\x18\x01 \x01(\tB\n\xea\x02\x07\x41::B::Cb\x06proto3"

pool = Google::Protobuf::DescriptorPool.generated_pool

begin
  pool.add_serialized_file(descriptor_data)
rescue TypeError => e
  # Compatibility code: will be removed in the next major version.
  require 'google/protobuf/descriptor_pb'
  parsed = Google::Protobuf::FileDescriptorProto.decode(descriptor_data)
  parsed.clear_dependency
  serialized = parsed.class.encode(parsed)
  file = pool.add_serialized_file(serialized)
  warn "Warning: Protobuf detected an import path issue while loading generated file #{__FILE__}"
  imports = [
  ]
  imports.each do |type_name, expected_filename|
    import_file = pool.lookup(type_name).file_descriptor
    if import_file.name != expected_filename
      warn "- #{file.name} imports #{expected_filename}, but that import was loaded as #{import_file.name}"
    end
  end
  warn "Each proto file must use a consistent fully-qualified name."
  warn "This will become an error in the next major version."
end

module A
  module B
    module C
      Four = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("one.two.a_three.Four").msgclass
    end
  end
end
