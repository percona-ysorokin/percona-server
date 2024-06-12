// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// https://developers.google.com/protocol-buffers/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/compiler/cpp/field.h"
#include "google/protobuf/compiler/cpp/helpers.h"
#include "google/protobuf/compiler/cpp/options.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/printer.h"

namespace google {
namespace protobuf {
namespace compiler {
namespace cpp {
namespace {
using Sub = ::google::protobuf::io::Printer::Sub;

std::vector<Sub> Vars(const FieldDescriptor* field, const Options& opts,
                      bool lite) {
  const auto* key = field->message_type()->map_key();
  const auto* val = field->message_type()->map_value();

  std::string key_type = PrimitiveTypeName(opts, key->cpp_type());
  std::string val_type;
  switch (val->cpp_type()) {
    case FieldDescriptor::CPPTYPE_MESSAGE:
      val_type = FieldMessageTypeName(val, opts);
      break;
    case FieldDescriptor::CPPTYPE_ENUM:
      val_type = ClassName(val->enum_type(), true);
      break;
    default:
      val_type = PrimitiveTypeName(opts, val->cpp_type());
      break;
  }

  return {
      {"Map", absl::Substitute("::google::protobuf::Map<$0, $1>", key_type, val_type)},
      {"Entry", ClassName(field->message_type(), false)},
      {"Key", PrimitiveTypeName(opts, key->cpp_type())},
      {"Val", val_type},
      {"MapField", lite ? "MapFieldLite" : "MapField"},
  };
}

class Map : public FieldGeneratorBase {
 public:
  Map(const FieldDescriptor* field, const Options& opts,
      MessageSCCAnalyzer* scc)
      : FieldGeneratorBase(field, opts),
        field_(field),
        key_(field->message_type()->map_key()),
        val_(field->message_type()->map_value()),
        opts_(&opts),
        has_required_(scc->HasRequiredFields(field->message_type())),
        lite_(!HasDescriptorMethods(field->file(), opts)) {}
  ~Map() override = default;

  std::vector<Sub> MakeVars() const override {
    return Vars(field_, *opts_, lite_);
  }

  void GenerateClearingCode(io::Printer* p) const override {
    p->Emit(R"cc(
      $field_$.Clear();
    )cc");
  }

  void GenerateMergingCode(io::Printer* p) const override {
    p->Emit(R"cc(
      _this->$field_$.MergeFrom(from.$field_$);
    )cc");
  }

  void GenerateSwappingCode(io::Printer* p) const override {
    p->Emit(R"cc(
      $field_$.InternalSwap(&other->$field_$);
    )cc");
  }

  void GenerateCopyConstructorCode(io::Printer* p) const override {
    GenerateConstructorCode(p);
    GenerateMergingCode(p);
  }

  void GenerateIsInitialized(io::Printer* p) const override {
    if (!has_required_) return;

    p->Emit(R"cc(
      if (!$pbi$::AllAreInitialized($field_$)) {
        return false;
      }
    )cc");
  }

  void GenerateConstexprAggregateInitializer(io::Printer* p) const override {
    p->Emit(R"cc(
      /* decltype($field_$) */ {},
    )cc");
  }

  void GenerateCopyAggregateInitializer(io::Printer* p) const override {
    // MapField has no move constructor, which prevents explicit aggregate
    // initialization pre-C++17.
    p->Emit(R"cc(
      /* decltype($field_$) */ {},
    )cc");
  }

  void GenerateAggregateInitializer(io::Printer* p) const override {
    if (ShouldSplit(field_, *opts_)) {
      p->Emit(R"cc(
        /* decltype($Msg$::Split::$name$_) */ {
            $pbi$::ArenaInitialized(),
            arena,
        },
      )cc");
    } else {
      p->Emit(R"cc(
        /* decltype($field_$) */ {$pbi$::ArenaInitialized(), arena},
      )cc");
    }
  }

  void GenerateConstructorCode(io::Printer* p) const override {}

  void GenerateDestructorCode(io::Printer* p) const override {
    if (ShouldSplit(field_, *opts_)) {
      p->Emit(R"cc(
        $cached_split_ptr$->$name$_.~$MapField$();
      )cc");
      return;
    }

    p->Emit(R"cc(
      $field_$.~$MapField$();
    )cc");
  }

  void GeneratePrivateMembers(io::Printer* p) const override;
  void GenerateAccessorDeclarations(io::Printer* p) const override;
  void GenerateInlineAccessorDefinitions(io::Printer* p) const override;
  void GenerateSerializeWithCachedSizesToArray(io::Printer* p) const override;
  void GenerateByteSize(io::Printer* p) const override;

 private:
  const FieldDescriptor* field_;
  const FieldDescriptor* key_;
  const FieldDescriptor* val_;
  const Options* opts_;
  bool has_required_;
  bool lite_;
};

void Map::GeneratePrivateMembers(io::Printer* p) const {
  p->Emit({{"kKeyType",
            absl::AsciiStrToUpper(DeclaredTypeMethodName(key_->type()))},
           {"kValType",
            absl::AsciiStrToUpper(DeclaredTypeMethodName(val_->type()))}},
          R"cc(
            $pbi$::$MapField$<$Entry$, $Key$, $Val$,
                              $pbi$::WireFormatLite::TYPE_$kKeyType$,
                              $pbi$::WireFormatLite::TYPE_$kValType$>
                $name$_;
          )cc");
}

void Map::GenerateAccessorDeclarations(io::Printer* p) const {
  auto v1 = p->WithVars(
      AnnotatedAccessors(field_, {"", "_internal_", "_internal_mutable_"}));
  auto v2 = p->WithVars(AnnotatedAccessors(field_, {"mutable_"},
                                           io::AnnotationCollector::kAlias));
  p->Emit(R"cc(
    $DEPRECATED$ const $Map$& $name$() const;
    $DEPRECATED$ $Map$* $mutable_name$();

    private:
    const $Map$& $_internal_name$() const;
    $Map$* $_internal_mutable_name$();

    public:
  )cc");
}

void Map::GenerateInlineAccessorDefinitions(io::Printer* p) const {
  p->Emit(R"cc(
    inline const $Map$& $Msg$::_internal_$name$() const {
      $TsanDetectConcurrentRead$;
      return $field_$.GetMap();
    }
    inline const $Map$& $Msg$::$name$() const {
      $annotate_get$;
      // @@protoc_insertion_point(field_map:$pkg.Msg.field$)
      return _internal_$name$();
    }
    inline $Map$* $Msg$::_internal_mutable_$name$() {
      $PrepareSplitMessageForWrite$;
      $TsanDetectConcurrentMutation$;
      return $field_$.MutableMap();
    }
    inline $Map$* $Msg$::mutable_$name$() {
      $annotate_mutable$;
      // @@protoc_insertion_point(field_mutable_map:$pkg.Msg.field$)
      return _internal_mutable_$name$();
    }
  )cc");
}

void Map::GenerateSerializeWithCachedSizesToArray(io::Printer* p) const {
  bool string_key = key_->type() == FieldDescriptor::TYPE_STRING;
  bool string_val = val_->type() == FieldDescriptor::TYPE_STRING;

  p->Emit(
      {
          {"Sorter", string_key ? "MapSorterPtr" : "MapSorterFlat"},
          {"CheckUtf8",
           [&] {
             if (string_key) {
               GenerateUtf8CheckCodeForString(
                   p, key_, *opts_, /*for_parse=*/false,
                   "entry.first.data(), "
                   "static_cast<int>(entry.first.length()),\n");
             }
             if (string_val) {
               GenerateUtf8CheckCodeForString(
                   p, val_, *opts_, /*for_parse=*/false,
                   "entry.second.data(), "
                   "static_cast<int>(entry.second.length()),\n");
             }
           }},
      },
      R"cc(
        if (!_internal_$name$().empty()) {
          using MapType = $Map$;
          using WireHelper = $Entry$::Funcs;
          const auto& field = _internal_$name$();

          if (stream->IsSerializationDeterministic() && field.size() > 1) {
            for (const auto& entry : $pbi$::$Sorter$<MapType>(field)) {
              target = WireHelper::InternalSerialize(
                  $number$, entry.first, entry.second, target, stream);
              $CheckUtf8$;
            }
          } else {
            for (const auto& entry : field) {
              target = WireHelper::InternalSerialize(
                  $number$, entry.first, entry.second, target, stream);
              $CheckUtf8$;
            }
          }
        }
      )cc");
}

void Map::GenerateByteSize(io::Printer* p) const {
  p->Emit(R"cc(
    total_size += $kTagBytes$ * $pbi$::FromIntSize(_internal_$name$_size());
    for (const auto& entry : _internal_$name$()) {
      total_size += $Entry$::Funcs::ByteSizeLong(entry.first, entry.second);
    }
  )cc");
}
}  // namespace

std::unique_ptr<FieldGeneratorBase> MakeMapGenerator(
    const FieldDescriptor* desc, const Options& options,
    MessageSCCAnalyzer* scc) {
  return std::make_unique<Map>(desc, options, scc);
}

}  // namespace cpp
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
