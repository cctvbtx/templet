/*

The MIT License (MIT)

Copyright (c) 2014 https://github.com/labyrinthofdreams

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>
#include "nodes.hpp"
#include "split.hpp"
#include "strutils.hpp"
#include "trim.hpp"

using namespace templet::nodes;

namespace {

bool isValidName(const std::string& name) {
    const auto genericNameValidator = [](const char c){
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || (c == '_' || c == '-');
    };

    return std::all_of(name.cbegin(), name.cend(), std::cref(genericNameValidator));
}

bool isValidNameExpression(const std::string& name) {
    const auto specialNameValidator = [](const char c){
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || (c == '_' || c == '-') ||
                (c == '[' || c == ']' || c == '.');
    };

    const bool hasMultiDot = name.find("..") != std::string::npos;
    const bool isValidSpecial = std::all_of(name.cbegin(), name.cend(), std::cref(specialNameValidator));

    return isValidSpecial && !hasMultiDot;
}

/**
 * @brief Parse a string into a number
 * @param text String to parse
 * @param results Save the parsed integer
 * @return True on success, otherwise false
 */
bool parse_number(const std::string& text, int& result) {
    std::istringstream parser {text};
    return parser >> result >> std::ws && parser.peek() == EOF;
}

/**
 * @brief Parse an array index
 *
 * Given a string like [5], returns 5
 *
 * Negative indexes are allowed
 *
 * @param in String to parse
 * @exception templet::exception::InvalidTagError
 * @return Index as an int
 */
int parse_array_index(std::string in) {
    if(!mylib::starts_with(in, "[") || !mylib::ends_with(in, "]")) {
        throw templet::exception::InvalidTagError("Invalid array syntax: Value must be enclosed with []");
    }

    int idx = 0;
    const auto raw_number = in.substr(1, in.size() - 2);
    const bool is_number = parse_number(raw_number, idx);
    if(!is_number) {
        throw templet::exception::InvalidTagError("Invalid array index: Value must be an integer");
    }

    return idx;
}

/**
 * @brief Parse a single tag name with optional array index
 *
 * Given a string config[5] returns pair("config", 5)
 *
 * @param in Tag name to parse as a string
 * @exception templet::exception::InvalidTagError if the index is negative
 * or if parse_array_index throws
 * @return Tag name and index value as a pair
 */
std::pair<std::string, int> parse_tag_name(std::string in) {
    // TODO: Add name validation here (e.g. checkibng final ])?
    const auto arr_pos = in.find('[');
    if(arr_pos == std::string::npos) {
        return {std::move(in), -1};
    }

    const int idx = parse_array_index(in.substr(arr_pos));
    if(idx < 0) {
        throw templet::exception::InvalidTagError("Invalid array index: Value must not be negative");
    }
    in.erase(arr_pos);

    return {std::move(in), idx};
}

/**
 * @brief Parse a full tag name
 *
 * Parses a full tag name, e.g.: config.servers[1].users[6].username
 *
 * @param name String to parse
 * @param kv Map of values to reference
 * @exception templet::exception::MissingTagError If tag is not found in kv
 * @exception templet::exception::InvalidTagError
 * @return Const-ref to DataPtr of the parsed tag name
 */
const templet::types::DataPtr& parse_tag(std::string name, const DataMap& kv) {
    auto mapRef = std::cref(kv);
    while(true) {
        const auto pos = name.find('.');
        // Parse the first found tag name
        const auto parsedName = parse_tag_name(name.substr(0, pos));
        const auto& mapName = parsedName.first;
        const auto& idx = parsedName.second;
        if(pos == std::string::npos) {
            name.clear();
        }
        else {
            name.erase(0, pos + 1);
        }

        const auto& map = mapRef.get();
        if(!map.count(mapName)) {
            throw templet::exception::MissingTagError("Tag name not found");
        }

        if(name.empty()) {
            // Retrieve the last tag
            if(idx < 0) {
                // Get DataPtr from map
                return map.at(mapName);
            }
            else {
                const auto& dataValue = map.at(mapName);
                if(dataValue->type() == templet::types::DataType::List) {
                    // Access item in a list
                    const auto& data = dataValue->getList();
                    if(idx >= data.size()) {
                        throw templet::exception::InvalidTagError("Index out of range");
                    }
                    return data.at(idx);
                }
                // Array indexing on strings
                else {
                    throw templet::exception::InvalidTagError("Only lists are supported by array indexes");
                }
//                else if(dataValue->type() == templet::types::DataType::String) {
//                    // Access character in a string
//                    const auto& data = dataValue->getValue();
//                    if(idx >= data.size()) {
//                        throw templet::exception::InvalidTagError("Index out of range");
//                    }
//                    return data.at(idx);
//                }
            }
        }

        // Note: This section applies only to names parsed before the last tag name
        // Ex: config.server.hostname (valid for 'config' and 'server', not 'hostname')
        // The found name may reference a direct map object (name)
        // or map object in a list (name[idx])
        const auto& found = map.at(mapName);
        if(found->type() == templet::types::DataType::Mapper) {
            mapRef = std::cref(found->getMap());
        }
        else if(found->type() == templet::types::DataType::List) {
            const auto& data = found->getList();
            if(idx >= data.size()) {
                throw templet::exception::InvalidTagError("Index out of range");
            }

            const auto& listObj = data.at(idx);
            // The found object in the list must be map
            if(listObj->type() != templet::types::DataType::Mapper) {
                throw templet::exception::InvalidTagError("Invalid index: Name does not match a map object");
            }
            mapRef = std::cref(listObj->getMap());
        }
        else {
            throw templet::exception::InvalidTagError("Invalid index: Name does not match a map object");
        }
    }
}

/**
 * @brief A helper function for \link parse_tag \endlink that evaluates into a string
 * @param name Tag name to parse
 * @param kv Values to reference
 * @exception templet::exception::InvalidTagError if result is not a string
 * @return Parsed result as a string
 */
std::string parse_tag_string(std::string name, const DataMap& kv) {
    const auto& res = parse_tag(std::move(name), kv);
    if(res->type() != templet::types::DataType::String) {
        throw templet::exception::InvalidTagError("Invalid tag name: Name must reference a string");
    }

    return res->getValue();
}

/**
 * @brief A helper function for \link parse_tag \endlink that evaluates into a vector
 * @param name Tag name to parse
 * @param kv Values to reference
 * @exception templet::exception::InvalidTagError if result is not a vector
 * @return Parsed result as a vector
 */
const templet::types::DataVector& parse_tag_list(std::string name, const DataMap& kv) {
    const auto& res = parse_tag(std::move(name), kv);
    if(res->type() != templet::types::DataType::List) {
        throw templet::exception::InvalidTagError("Invalid tag name: Name must reference a list");
    }

    return res->getList();
}

} // unnamed namespace

void Node::setChildren(std::vector<std::shared_ptr<Node>> /*children*/) {
    throw std::runtime_error("This Node type cannot have children");
}

NodeType Node::type() const {
    return NodeType::Invalid;
}

Text::Text(std::string text)
    : Node(), _in(std::move(text)) {

}

void Text::evaluate(std::ostream& os, const DataMap& /*kv*/) const {
    os << _in;
}

NodeType Text::type() const {
    return NodeType::Text;
}

Value::Value(std::string name)
    : Node(), _name(std::move(name)) {
    if(!isValidNameExpression(_name)) {
        throw templet::exception::InvalidTagError("Variable tag name contains invalid characters");
    }
}

void Value::evaluate(std::ostream& os, const DataMap& kv) const {
    try {
        os << parse_tag_string(_name, kv);
    }
    catch(const templet::exception::MissingTagError& ex) {
        // Default behavior is to just ignore it, effectively
        // just removing the tag name from the output
        // TODO: Add user config option to re-throw
    }
    catch(...) {
        throw;
    }
}

NodeType Value::type() const {
    return NodeType::Value;
}

IfValue::IfValue(std::string name)
    : Node(), _name(std::move(name)), _nodes() {
    if(!isValidNameExpression(_name)) {
        throw templet::exception::InvalidTagError("If expression tag name contains invalid characters");
    }
}

void IfValue::setChildren(std::vector<std::shared_ptr<Node>> children) {
    _nodes.swap(children);
}

void IfValue::evaluate(std::ostream& os, const DataMap& kv) const {
    // Check that the IF condition is TRUE (it's enough that it's been set)
    if(kv.count(_name)) {
        for(auto& node : _nodes) {
            if(node->type() == templet::nodes::NodeType::ElifValue ||
                    node->type() == templet::nodes::NodeType::ElseValue) {
                break;
            }
            node->evaluate(os, kv);
        }
    }
    else {
        // Do Elif/else block
        for(auto& node : _nodes) {
            if(node->type() == templet::nodes::NodeType::ElifValue ||
                    node->type() == templet::nodes::NodeType::ElseValue) {
                node->evaluate(os, kv);
            }
        }
    }
}

NodeType IfValue::type() const {
    return NodeType::IfValue;
}

ElifValue::ElifValue(std::string name)
    : IfValue(std::move(name)) {

}

NodeType ElifValue::type() const {
    return NodeType::ElifValue;
}

ElseValue::ElseValue() : _nodes() {

}

void ElseValue::setChildren(std::vector<std::shared_ptr<Node> > children) {
    _nodes.swap(children);
}

void ElseValue::evaluate(std::ostream& os, const templet::types::DataMap& kv) const {
    for(auto& node : _nodes) {
        node->evaluate(os, kv);
    }
}

NodeType ElseValue::type() const {
    return NodeType::ElseValue;
}


ForValue::ForValue(std::string name, std::string alias)
    : _name(std::move(name)), _alias(std::move(alias)), _nodes() {
    // Validate names
    if(!isValidNameExpression(_name)) {
        throw templet::exception::InvalidTagError("For expression first tag name contains invalid characters");
    }
    else if(!isValidName(_alias)) {
        throw templet::exception::InvalidTagError("For expression second tag name contains invalid characters");
    }
}

void ForValue::setChildren(std::vector<std::shared_ptr<Node>> children) {
    _nodes.swap(children);
}

void ForValue::evaluate(std::ostream& os, const templet::types::DataMap& kv) const {
    const auto& evaluatedList = parse_tag_list(_name, kv);
    if(kv.count(_alias)) {
        throw templet::exception::InvalidTagError("For expression alias name collides with an existing name");
    }
    // In a for statement the map entries are copied
    // and the 'as' values are added with the new name
    templet::types::DataMap newValues = kv;
    for(const auto& item : evaluatedList) {
        newValues[_alias] = item;
        for(auto& node : _nodes) {
            node->evaluate(os, newValues);
        }
    }
}

NodeType ForValue::type() const {
    return NodeType::ForValue;
}

std::shared_ptr<Node> templet::nodes::parse_value_tag(std::string in) {
    if(!mylib::starts_with(in, "{$") || !mylib::ends_with(in, "}")) {
        throw templet::exception::InvalidTagError("Tag must be enclosed with {$ and }");
    }

    // Remove surrounding {$ and }
    in.erase(0, 2);
    in.erase(in.find('}'));
    in = mylib::trim(in);

    return std::make_shared<Value>(std::move(in));
}

std::shared_ptr<Node> templet::nodes::parse_ifvalue_tag(std::string in) {
    if(!mylib::starts_with(in, "{%") || !mylib::ends_with(in, "%}")) {
        throw templet::exception::InvalidTagError("Tag must be enclosed with {% and %}");
    }

    in.erase(0, 2);
    in.erase(in.find('%'));
    in = mylib::trim(in);
    if(!mylib::starts_with(in, "if ")) {
        throw templet::exception::InvalidTagError("Tag must be prefixed with 'if '");
    }

    in = mylib::ltrim(in.erase(0, in.find(' ') + 1));

    // TODO: Validate the variable name either via IfValue::is_valid_tag()
    // or in the IfValue constructor
    return std::make_shared<IfValue>(std::move(in));
}

std::shared_ptr<Node> templet::nodes::parse_elifvalue_tag(std::string in) {
    if(!mylib::starts_with(in, "{%") || !mylib::ends_with(in, "%}")) {
        throw templet::exception::InvalidTagError("Tag must be enclosed with {% and %}");
    }

    in.erase(0, 2);
    in.erase(in.find('%'));
    in = mylib::trim(in);
    if(!mylib::starts_with(in, "elif ")) {
        throw templet::exception::InvalidTagError("Tag must be prefixed with 'elif '");
    }

    in = mylib::ltrim(in.erase(0, in.find(' ') + 1));

    // TODO: Validate the variable name either via IfValue::is_valid_tag()
    // or in the IfValue constructor
    return std::make_shared<ElifValue>(std::move(in));
}

std::shared_ptr<Node> templet::nodes::parse_forvalue_tag(std::string in)
{
    if(!mylib::starts_with(in, "{%") || !mylib::ends_with(in, "%}")) {
        throw templet::exception::InvalidTagError("Tag must be enclosed with {% and %}");
    }

    in.erase(0, 2);
    in.erase(in.find('%'));
    in = mylib::trim(in);
    auto tokens = mylib::split(in, ' ');
    if(tokens.size() != 4) {
        throw templet::exception::ExpressionSyntaxError("Unrecognized for expression syntax");
    }

    if(tokens[0] != "for" || tokens[2] != "as") {
        throw templet::exception::ExpressionSyntaxError("Unrecognized for expression syntax");
    }

    return std::make_shared<ForValue>(tokens[1], tokens[3]);
}
