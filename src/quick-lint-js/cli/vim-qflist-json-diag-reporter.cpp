// Copyright (C) 2020  Matthew "strager" Glazar
// See end of file for extended copyright information.

#include <quick-lint-js/cli/vim-qflist-json-diag-reporter.h>
#include <quick-lint-js/container/optional.h>
#include <quick-lint-js/container/padded-string.h>
#include <quick-lint-js/diag/diagnostic-types.h>
#include <quick-lint-js/fe/source-code-span.h>
#include <quick-lint-js/fe/token.h>
#include <quick-lint-js/io/output-stream.h>
#include <quick-lint-js/json.h>
#include <quick-lint-js/port/unreachable.h>
#include <string>

using namespace std::literals::string_view_literals;

namespace quick_lint_js {
Vim_QFList_JSON_Diag_Reporter::Vim_QFList_JSON_Diag_Reporter(
    Translator t, Output_Stream *output)
    : output_(*output), translator_(t) {
  this->output_.append_literal(u8"{\"qflist\": ["_sv);
}

void Vim_QFList_JSON_Diag_Reporter::set_source(Padded_String_View input,
                                               const char *file_name,
                                               int vim_bufnr) {
  this->set_source(input, /*file_name=*/file_name,
                   /*vim_bufnr=*/std::optional<int>(vim_bufnr));
}

void Vim_QFList_JSON_Diag_Reporter::set_source(Padded_String_View input,
                                               const char *file_name,
                                               std::optional<int> vim_bufnr) {
  this->locator_.emplace(input);
  this->file_name_ = file_name;
  this->bufnr_ = vim_bufnr.has_value() ? std::to_string(*vim_bufnr) : "";
}

void Vim_QFList_JSON_Diag_Reporter::set_source(Padded_String_View input,
                                               const char *file_name) {
  this->set_source(input, /*file_name=*/file_name, /*vim_bufnr=*/std::nullopt);
}

void Vim_QFList_JSON_Diag_Reporter::set_source(Padded_String_View input,
                                               int vim_bufnr) {
  this->locator_.emplace(input);
  this->file_name_.clear();
  this->bufnr_ = std::to_string(vim_bufnr);
}

void Vim_QFList_JSON_Diag_Reporter::finish() {
  this->output_.append_literal(u8"]}"_sv);
}

void Vim_QFList_JSON_Diag_Reporter::report(const Diag_List &diags) {
  diags.for_each([&](Diag_Type type, void *diag) -> void {
    if (this->need_comma_) {
      this->output_.append_literal(u8",\n"_sv);
    }
    this->need_comma_ = true;
    QLJS_ASSERT(this->locator_.has_value());
    Vim_QFList_JSON_Diag_Formatter formatter(this->translator_,
                                             /*output=*/&this->output_,
                                             /*locator=*/*this->locator_,
                                             /*file_name=*/this->file_name_,
                                             /*bufnr=*/this->bufnr_);
    formatter.format(get_diagnostic_info(type), diag);
  });
}

Vim_QFList_JSON_Diag_Formatter::Vim_QFList_JSON_Diag_Formatter(
    Translator t, Output_Stream *output, Vim_Locator &locator,
    std::string_view file_name, std::string_view bufnr)
    : Diagnostic_Formatter(t),
      output_(*output),
      locator_(locator),
      file_name_(file_name),
      bufnr_(bufnr) {}

void Vim_QFList_JSON_Diag_Formatter::write_before_message(
    std::string_view code, Diagnostic_Severity sev,
    const Source_Code_Span &origin) {
  String8_View severity_type{};
  switch (sev) {
  case Diagnostic_Severity::error:
    severity_type = u8"E"_sv;
    break;
  case Diagnostic_Severity::note:
    // Don't write notes. Only write the main message.
    return;
  case Diagnostic_Severity::warning:
    severity_type = u8"W"_sv;
    break;
  }

  Vim_Source_Range r = this->locator_.range(origin);
  auto end_col = origin.begin() == origin.end() ? r.begin.col : (r.end.col - 1);
  this->output_.append_literal(u8"{\"col\": "_sv);
  this->output_.append_decimal_integer(r.begin.col);
  this->output_.append_literal(u8", \"lnum\": "_sv);
  this->output_.append_decimal_integer(r.begin.lnum);
  this->output_.append_literal(u8", \"end_col\": "_sv);
  this->output_.append_decimal_integer(end_col);
  this->output_.append_literal(u8", \"end_lnum\": "_sv);
  this->output_.append_decimal_integer(r.end.lnum);
  this->output_.append_literal(u8", \"type\": \""_sv);
  this->output_.append_copy(severity_type);
  this->output_.append_literal(u8"\", \"nr\": \""_sv);
  this->output_.append_copy(to_string8_view(code));
  this->output_.append_literal(u8"\", \"vcol\": 0, \"text\": \""_sv);
}

void Vim_QFList_JSON_Diag_Formatter::write_message_part(
    [[maybe_unused]] std::string_view code, Diagnostic_Severity sev,
    String8_View message) {
  if (sev == Diagnostic_Severity::note) {
    // Don't write notes. Only write the main message.
    return;
  }

  write_json_escaped_string(this->output_, message);
}

void Vim_QFList_JSON_Diag_Formatter::write_after_message(
    [[maybe_unused]] std::string_view code, Diagnostic_Severity sev,
    const Source_Code_Span &) {
  if (sev == Diagnostic_Severity::note) {
    // Don't write notes. Only write the main message.
    return;
  }

  this->output_.append_copy(u8'\"');
  if (!this->bufnr_.empty()) {
    this->output_.append_literal(u8", \"bufnr\": "_sv);
    this->output_.append_copy(to_string8_view(this->bufnr_));
  }
  if (!this->file_name_.empty()) {
    this->output_.append_literal(u8", \"filename\": \""_sv);
    write_json_escaped_string(this->output_, to_string8_view(this->file_name_));
    this->output_.append_copy(u8'"');
  }
  this->output_.append_copy(u8'}');

  // If we don't flush, output is buffered. A lot of messages (e.g. over 4 KiB)
  // will fill the buffer and possibly force a flush in the middle of a message.
  // Then, a crash in the future (e.g. an assertion failure) will leave an
  // incomplete message written to the client. The client will have a hard time
  // extracting partial information from the incomplete JSON.
  //
  // If we flush now, it's less likely that a message ends up on the client. The
  // client can easily recover by adding a '}' at the end of the input to make
  // the incomplete JSON valid.
  this->output_.flush();
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
