# Generated by the protocol buffer compiler.  DO NOT EDIT!
# source: ruby_generated_code_proto2.proto

require 'google/protobuf'

Google::Protobuf::DescriptorPool.generated_pool.build do
  add_file("ruby_generated_code_proto2.proto", :syntax => :proto2) do
    add_message "A.B.C.TestMessage" do
      optional :optional_int32, :int32, 1, default: 1
      optional :optional_int64, :int64, 2, default: 2
      optional :optional_uint32, :uint32, 3, default: 3
      optional :optional_uint64, :uint64, 4, default: 4
      optional :optional_bool, :bool, 5, default: true
      optional :optional_double, :double, 6, default: 6
      optional :optional_float, :float, 7, default: 7
      optional :optional_string, :string, 8, default: "default str"
      optional :optional_bytes, :bytes, 9, default: "\x00\x01\x02\x40\x66\x75\x62\x61\x72".force_encoding("ASCII-8BIT")
      optional :optional_enum, :enum, 10, "A.B.C.TestEnum", default: 1
      optional :optional_msg, :message, 11, "A.B.C.TestMessage"
      repeated :repeated_int32, :int32, 21
      repeated :repeated_int64, :int64, 22
      repeated :repeated_uint32, :uint32, 23
      repeated :repeated_uint64, :uint64, 24
      repeated :repeated_bool, :bool, 25
      repeated :repeated_double, :double, 26
      repeated :repeated_float, :float, 27
      repeated :repeated_string, :string, 28
      repeated :repeated_bytes, :bytes, 29
      repeated :repeated_enum, :enum, 30, "A.B.C.TestEnum"
      repeated :repeated_msg, :message, 31, "A.B.C.TestMessage"
      required :required_int32, :int32, 41
      required :required_int64, :int64, 42
      required :required_uint32, :uint32, 43
      required :required_uint64, :uint64, 44
      required :required_bool, :bool, 45
      required :required_double, :double, 46
      required :required_float, :float, 47
      required :required_string, :string, 48
      required :required_bytes, :bytes, 49
      required :required_enum, :enum, 50, "A.B.C.TestEnum"
      required :required_msg, :message, 51, "A.B.C.TestMessage"
      optional :nested_message, :message, 80, "A.B.C.TestMessage.NestedMessage"
      oneof :my_oneof do
        optional :oneof_int32, :int32, 61
        optional :oneof_int64, :int64, 62
        optional :oneof_uint32, :uint32, 63
        optional :oneof_uint64, :uint64, 64
        optional :oneof_bool, :bool, 65
        optional :oneof_double, :double, 66
        optional :oneof_float, :float, 67
        optional :oneof_string, :string, 68
        optional :oneof_bytes, :bytes, 69
        optional :oneof_enum, :enum, 70, "A.B.C.TestEnum"
        optional :oneof_msg, :message, 71, "A.B.C.TestMessage"
      end
    end
    add_message "A.B.C.TestMessage.NestedMessage" do
      optional :foo, :int32, 1
    end
    add_enum "A.B.C.TestEnum" do
      value :Default, 0
      value :A, 1
      value :B, 2
      value :C, 3
    end
  end
end

module A
  module B
    module C
      TestMessage = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("A.B.C.TestMessage").msgclass
      TestMessage::NestedMessage = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("A.B.C.TestMessage.NestedMessage").msgclass
      TestEnum = ::Google::Protobuf::DescriptorPool.generated_pool.lookup("A.B.C.TestEnum").enummodule
    end
  end
end
