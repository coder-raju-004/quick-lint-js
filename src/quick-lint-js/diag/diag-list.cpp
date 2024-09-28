// Copyright (C) 2020  Matthew "strager" Glazar
// See end of file for extended copyright information.

#include <quick-lint-js/diag/diag-list.h>
#include <quick-lint-js/port/memory-resource.h>
#include <quick-lint-js/util/algorithm.h>

namespace quick_lint_js {
struct Diag_List::Node : public Node_Base {
  union Underlying_Diag {
#define QLJS_DIAG_TYPE_NAME(name)                                    \
  ::quick_lint_js::name name;                                        \
  static_assert(std::is_trivially_copyable_v<::quick_lint_js::name>, \
                #name " should be trivially copyable");
    QLJS_X_DIAG_TYPE_NAMES
#undef QLJS_DIAG_TYPE_NAME
  };

  alignas(Underlying_Diag) char data[sizeof(Underlying_Diag)];
};
static_assert(alignof(Diag_List::Node) == alignof(Diag_List::Node_Base),
              "Node_Base alignment should align any Diag type");

Diag_List::Diag_List(Memory_Resource *memory) : memory_(memory) {}

Diag_List::Diag_List(Diag_List &&other)
    : memory_(other.memory_), first_(other.first_), last_(other.last_) {
  other.clear();
}

Diag_List::~Diag_List() { this->clear(); }

void Diag_List::add_many(const Diag_List &other) {
  other.for_each([&](Diag_Type diag_type, void *diag_data) {
    // FIXME(strager): This wastes memory.
    std::size_t diag_size = sizeof(Diag_List::Node::data);
    this->add_impl(diag_type, diag_data, diag_size);
  });
}

Diag_List::Rewind_State Diag_List::prepare_for_rewind() {
  return Rewind_State{
      .first_ = this->first_,
      .last_ = this->last_,
  };
}

void Diag_List::rewind(Rewind_State &&r) {
  // Leak nodes between r.last and this->last_. this->memory should be a
  // Linked_Bump_Allocator managed by the caller.
  this->first_ = r.first_;
  this->last_ = r.last_;
}

#define QLJS_DIAG_TYPE_NAME(name)                         \
  void Diag_List::add(name diag) {                        \
    this->add_impl<sizeof(diag)>(Diag_Type::name, &diag); \
  }
QLJS_X_DIAG_TYPE_NAMES
#undef QLJS_DIAG_TYPE_NAME

template <std::size_t diag_size>
void Diag_List::add_impl(Diag_Type type, void *diag) {
  this->add_impl(type, diag, diag_size);
}

void Diag_List::add_impl(Diag_Type type, void *diag, std::size_t diag_size) {
  Node *node = this->memory_->new_object<Node>();
  node->next = nullptr;
  node->type = type;
  std::memcpy(&node->data, diag, diag_size);
  if (this->last_ == nullptr) {
    this->first_ = node;
  } else {
    this->last_->next = node;
  }
  this->last_ = node;
}

bool Diag_List::empty() const { return this->first_ == nullptr; }

Span_Size Diag_List::size() const {
  Span_Size count = 0;
  this->for_each([&](auto &&...) { count += 1; });
  return count;
}

bool Diag_List::reported_any_diagnostic_except_since(
    std::initializer_list<Diag_Type> ignored_types,
    const Rewind_State &r) const {
  for (Node_Base *node = r.last_ == nullptr ? this->first_ : r.last_;
       node != nullptr; node = node->next) {
    if (!contains(ignored_types, node->type)) {
      return true;
    }
  }
  return false;
}

bool Diag_List::have_diagnostic(Diag_Type type) const {
  for (Node_Base *node = this->first_; node != nullptr; node = node->next) {
    if (node->type == type) {
      return true;
    }
  }
  return false;
}

void Diag_List::clear() {
  // Leak. this->memory should be a Linked_Bump_Allocator managed by the caller.
  this->first_ = nullptr;
  this->last_ = nullptr;
}
}

// quick-lint-js finds bugs in JavaScript programs.
// Copyright (C) 2020  Matthew "strager" Glazar
//
// This file is part of quick-lint-js.
//
// quick-lint-js is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// quick-lint-js is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with quick-lint-js.  If not, see <https://www.gnu.org/licenses/>.
