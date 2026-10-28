#include "expression_tree.h"

#include <algorithm>
#include <cctype>

#include "core/util.h"

// extract the argument text of a probe name, e.g. "V(XU1:23)" -> "XU1:23";
// returns an empty view when the name is not a simple "(...)" probe — the
// text before the parenthesis must be a plain alphabetic function name so
// compound expressions like "2*I(XU1:R1)" stay flat
static std::string_view probe_argument(std::string_view name) {
    // empty names have no argument
    if (name.empty())
        return {};
    // locate the opening parenthesis
    const size_t open = name.find('(');
    if (open == std::string_view::npos)
        return {};
    // the function prefix must be a plain alphabetic name
    for (size_t i = 0; i < open; ++i) {
        if (!std::isalpha(static_cast<unsigned char>(name[i])))
            return {};
    }
    // the argument list must end at the last character
    if (name.back() != ')' || name.find(')', open + 1) != name.size() - 1)
        return {};
    // extract the text inside the parentheses
    return name.substr(open + 1, name.size() - open - 2);
}

std::pair<GroupKind, std::vector<std::string>> ExpressionTree::parse(std::string_view name) {
    // extract the probe argument holding the node path
    std::string_view inside = probe_argument(name);
    // names without a probe argument are flat
    if (inside.empty())
        return {GroupKind::Flat, {}};
    // differential probes list two nodes, the hierarchy comes from the first
    if (const size_t comma = inside.find(','); comma != std::string_view::npos)
        inside = inside.substr(0, comma);
    // sheet hierarchy: slash separated segments with a leading slash
    if (inside.front() == '/') {
        // split the path into its non-empty segments
        const std::vector<std::string_view> segments = split_by(inside, '/');
        // a single segment after the slash is a plain net name
        if (segments.size() < 2)
            return {GroupKind::Flat, {}};
        // every segment but the last one is a scope
        std::vector<std::string> groups(segments.begin(), segments.end() - 1);
        return {GroupKind::Sheet, std::move(groups)};
    }
    // subcircuit hierarchy: colon separated instance path
    const std::vector<std::string_view> segments = split_by(inside, ':');
    // a single segment is a plain node name
    if (segments.size() < 2)
        return {GroupKind::Flat, {}};
    // every segment but the last one is a scope
    std::vector<std::string> groups(segments.begin(), segments.end() - 1);
    return {GroupKind::Subcircuit, std::move(groups)};
}

GroupKind ExpressionTree::kind_of(std::string_view name) {
    // resolve the classification only
    return parse(name).first;
}

std::vector<std::string> ExpressionTree::hierarchy_of(std::string_view name) {
    // resolve the hierarchy segments only
    return parse(name).second;
}

ExpressionTree::Leaf* ExpressionTree::find_leaf(Group& group, const std::string& name) {
    // search the directly attached leaves
    for (Leaf& leaf : group.leaves) {
        if (leaf.name == name)
            return &leaf;
    }
    // search the child scope subtrees
    for (Group& child : group.children) {
        if (Leaf* found = find_leaf(child, name))
            return found;
    }
    // not found in this subtree
    return nullptr;
}

const ExpressionTree::Leaf* ExpressionTree::find_leaf(const Group& group, const std::string& name) {
    // search the directly attached leaves
    for (const Leaf& leaf : group.leaves) {
        if (leaf.name == name)
            return &leaf;
    }
    // search the child scope subtrees
    for (const Group& child : group.children) {
        if (const Leaf* found = find_leaf(child, name))
            return found;
    }
    // not found in this subtree
    return nullptr;
}

void ExpressionTree::collect_selected(const Group& group, std::vector<std::string>& out) {
    // collect the directly attached selected leaves
    for (const Leaf& leaf : group.leaves) {
        if (leaf.selected)
            out.push_back(leaf.name);
    }
    // recurse into the child scopes
    for (const Group& child : group.children)
        collect_selected(child, out);
}

void ExpressionTree::collect_selected_leaves(Group& group, std::vector<Leaf*>& out) {
    // collect the directly attached selected leaves
    for (Leaf& leaf : group.leaves) {
        if (leaf.selected)
            out.push_back(&leaf);
    }
    // recurse into the child scopes
    for (Group& child : group.children)
        collect_selected_leaves(child, out);
}

void ExpressionTree::collect_matching(Group& group, std::vector<Leaf*>& out) {
    // collect the directly attached leaves matching the filter
    for (Leaf& leaf : group.leaves) {
        if (matches(leaf))
            out.push_back(&leaf);
    }
    // recurse into the child scopes
    for (Group& child : group.children)
        collect_matching(child, out);
}

int ExpressionTree::total_count(const Group& group) const {
    // count the directly attached leaves
    int count = static_cast<int>(group.leaves.size());
    // add the counts of all child scopes
    for (const Group& child : group.children)
        count += total_count(child);
    return count;
}

bool ExpressionTree::matches(const Leaf& leaf) const {
    // an empty filter matches everything
    if (m_filter.empty())
        return true;
    // case-insensitive substring match of the full name
    return to_lower(leaf.name).find(m_filter) != std::string::npos;
}

void ExpressionTree::insert(Leaf leaf) {
    // resolve the hierarchy path of the expression
    const auto [kind, segments] = parse(leaf.name);
    // assign the insertion order reproducing the raw file order
    leaf.order = m_next_order++;
    // scope kind character prefixing the persistence key, keeps sheet and
    // subcircuit scopes with equal labels in separate keys
    std::string key(1, kind == GroupKind::Sheet ? '/' : ':');
    // walk the root group
    Group* group = &m_root;
    // descend creating missing scopes along the path
    for (const std::string& segment : segments) {
        // find the existing child scope for this segment
        Group* child = nullptr;
        for (Group& candidate : group->children) {
            if (candidate.label == segment && candidate.kind == kind) {
                child = &candidate;
                break;
            }
        }
        // create the missing scope
        if (child == nullptr) {
            Group new_group;
            new_group.label = segment;
            new_group.kind = kind;
            // persistence key of this scope level
            new_group.key = key + segment;
            group->children.push_back(std::move(new_group));
            child = &group->children.back();
        }
        // extend the persistence key for the next level
        key = child->key + "/";
        // descend into the child scope
        group = child;
    }
    // attach the leaf to the final scope
    group->leaves.push_back(std::move(leaf));
}

ExpressionTree::Group* ExpressionTree::find_by_key_in(Group& group, const std::string& key) {
    // search the child scopes of this level
    for (Group& child : group.children) {
        // direct hit
        if (child.key == key)
            return &child;
        // recurse into the subtree
        if (Group* found = find_by_key_in(child, key))
            return found;
    }
    // key not found in this subtree
    return nullptr;
}

ExpressionTree::Group* ExpressionTree::find_by_key(const std::string& key) {
    // search the whole tree for the scope with this key
    return find_by_key_in(m_root, key);
}

ExpressionTree::Group* ExpressionTree::current_group() {
    // the root scope when the stack is empty
    if (m_scope_stack.empty())
        return &m_root;
    // the deepest scope on the stack
    if (Group* group = find_by_key(m_scope_stack.back().first))
        return group;
    // the scope vanished, fall back to the root
    return &m_root;
}

std::string ExpressionTree::display_label(const Leaf& leaf) const {
    // locate the probe function prefix
    const size_t open = leaf.name.find('(');
    if (open == std::string::npos || leaf.name.back() != ')')
        return leaf.name;
    // extract the function prefix and the argument text
    const std::string function_prefix = leaf.name.substr(0, open);
    std::string inside(leaf.name.substr(open + 1, leaf.name.size() - open - 2));
    // resolve the hierarchy kind of the leaf
    const GroupKind kind = parse(leaf.name).first;
    // flat leaves keep the full name
    if (kind == GroupKind::Flat)
        return leaf.name;
    // build the browsed scope prefix from the stack labels
    std::string prefix;
    for (const auto& [key, label] : m_scope_stack) {
        // sheet scopes join with slashes and lead with one
        if (kind == GroupKind::Sheet)
            prefix += "/" + label;
        // subcircuit scopes join with colons and trail with one
        else
            prefix += (prefix.empty() ? "" : ":") + label;
    }
    // trail the prefix with the kind separator
    prefix += kind == GroupKind::Sheet ? "/" : ":";
    // strip the browsed scope prefix from each comma-separated probe operand
    std::string result;
    size_t start = 0;
    while (start < inside.size()) {
        // locate the next comma (operand separator)
        const size_t comma = inside.find(',', start);
        const size_t end = comma == std::string::npos ? inside.size() : comma;
        // strip the prefix from this operand
        std::string operand = inside.substr(start, end - start);
        if (operand.rfind(prefix, 0) == 0)
            operand = operand.substr(prefix.size());
        // append the operand, separated from the previous one by a comma
        if (!result.empty())
            result += ",";
        result += operand;
        // advance past the comma
        start = comma == std::string::npos ? inside.size() : comma + 1;
    }
    // rebuild the display label with the shortened argument
    return function_prefix + "(" + result + ")";
}

void ExpressionTree::emit_scope_cards() {
    // emit the expression cards of the current scope
    Group& group = *current_group();
    for (Leaf& leaf : group.leaves) {
        ExpressionCard card;
        card.label = display_label(leaf);
        card.full_name = leaf.name;
        card.type = leaf.type;
        card.selected = leaf.selected;
        m_cards.push_back(std::move(card));
        CardMeta meta;
        meta.leaf = &leaf;
        m_card_meta.push_back(meta);
    }
    // emit the scope cards of the child scopes
    for (Group& child : group.children) {
        ExpressionCard card;
        card.label = child.label;
        card.kind = child.kind == GroupKind::Sheet ? "sheet" : "subcircuit";
        card.is_scope = true;
        card.count = total_count(child);
        card.scope_key = child.key;
        m_cards.push_back(std::move(card));
        CardMeta meta;
        meta.scope_key = child.key;
        meta.scope_label = child.label;
        m_card_meta.push_back(meta);
    }
}

void ExpressionTree::emit_selected_cards() {
    // collect the selected leaves of the whole tree, pre-order
    std::vector<Leaf*> collected;
    collect_selected_leaves(m_root, collected);
    // sort by insertion order, reproducing the raw file order
    std::stable_sort(collected.begin(), collected.end(), [](const Leaf* a, const Leaf* b) { return a->order < b->order; });
    // emit one flat expression card per selection
    for (Leaf* leaf : collected) {
        ExpressionCard card;
        card.label = leaf->name;
        card.full_name = leaf->name;
        card.type = leaf->type;
        card.selected = true;
        m_cards.push_back(std::move(card));
        CardMeta meta;
        meta.leaf = leaf;
        m_card_meta.push_back(meta);
    }
}

void ExpressionTree::build_breadcrumb() {
    // breadcrumb path of the browsed scope
    m_breadcrumb.clear();
    BreadcrumbEntry root_entry;
    root_entry.label = "All";
    root_entry.level = 0;
    m_breadcrumb.push_back(std::move(root_entry));
    for (size_t i = 0; i < m_scope_stack.size(); ++i) {
        BreadcrumbEntry entry;
        entry.label = m_scope_stack[i].second;
        entry.level = i + 1;
        m_breadcrumb.push_back(std::move(entry));
    }
}

void ExpressionTree::emit_filter_cards() {
    // collect the leaves matching the filter, pre-order
    std::vector<Leaf*> collected;
    collect_matching(m_root, collected);
    // sort by insertion order, reproducing the raw file order
    std::stable_sort(collected.begin(), collected.end(), [](const Leaf* a, const Leaf* b) { return a->order < b->order; });
    // emit one flat expression card per match
    for (Leaf* leaf : collected) {
        ExpressionCard card;
        card.label = leaf->name;
        card.full_name = leaf->name;
        card.type = leaf->type;
        card.selected = leaf->selected;
        m_cards.push_back(std::move(card));
        CardMeta meta;
        meta.leaf = leaf;
        m_card_meta.push_back(meta);
    }
}

void ExpressionTree::update_cards() {
    // reset the visible cards and their metadata
    m_cards.clear();
    m_card_meta.clear();
    // filtering flattens every matching expression into cards
    if (!m_filter.empty()) {
        emit_filter_cards();
        // no breadcrumb while filtering, the scope context is suspended
        m_breadcrumb.clear();
        return;
    }
    // the breadcrumb stays visible in both the scoped and selected views
    build_breadcrumb();
    // selected-only view flattens every selected expression into cards
    if (m_show_selected)
        emit_selected_cards();
    // browsing emits the cards of the current scope
    else
        emit_scope_cards();
}

void ExpressionTree::rebuild(const std::vector<std::pair<std::string, std::string>>& items) {
    // reset the tree and return to the root scope
    m_root = Group{};
    m_next_order = 0;
    m_scope_stack.clear();
    m_show_selected = false;
    m_filter.clear();
    m_cards.clear();
    m_card_meta.clear();
    m_breadcrumb.clear();
    // insert every expression along its hierarchy path
    for (const auto& [name, type] : items) {
        Leaf leaf;
        leaf.name = name;
        leaf.type = type;
        insert(std::move(leaf));
    }
    // recompute the visible cards
    update_cards();
}

void ExpressionTree::add_leaf(std::string name, std::string type) {
    // build the leaf and attach it along its hierarchy path
    Leaf leaf;
    leaf.name = std::move(name);
    leaf.type = std::move(type);
    insert(std::move(leaf));
    // recompute the visible cards
    update_cards();
}

bool ExpressionTree::contains(const std::string& name) const {
    // search the whole tree for a leaf with this name
    return find_leaf(m_root, name) != nullptr;
}

void ExpressionTree::set_selected(const std::string& name, bool selected) {
    // locate the leaf and update its flag
    if (Leaf* leaf = find_leaf(m_root, name)) {
        leaf->selected = selected;
        // recompute the visible cards, they carry the selection state
        update_cards();
    }
}

std::vector<std::string> ExpressionTree::selected_names() const {
    // collect the selected leaf names in tree order
    std::vector<std::string> names;
    collect_selected(m_root, names);
    return names;
}

void ExpressionTree::set_filter(std::string query) {
    // store the filter in its case-insensitive form
    m_filter = to_lower(query);
    // recompute the visible cards
    update_cards();
}

void ExpressionTree::activate(size_t card_index) {
    // ignore out-of-range cards
    if (card_index >= m_cards.size())
        return;
    // resolve the card metadata
    const CardMeta& meta = m_card_meta[card_index];
    // scope cards drill into the group
    if (meta.leaf == nullptr)
        m_scope_stack.emplace_back(meta.scope_key, meta.scope_label);
    // expression cards toggle their selection
    else
        meta.leaf->selected = !meta.leaf->selected;
    // recompute the visible cards
    update_cards();
}

void ExpressionTree::set_show_selected(bool show) {
    // store the selected-only view flag
    m_show_selected = show;
    // recompute the visible cards
    update_cards();
}

bool ExpressionTree::show_selected() const {
    // selected-only view state
    return m_show_selected;
}

void ExpressionTree::open_breadcrumb(size_t level) {
    // levels beyond the current depth are ignored
    if (level > m_scope_stack.size())
        return;
    // navigating out of the scope exits the selected-only view
    m_show_selected = false;
    // restore the scope stack to the requested level
    m_scope_stack.resize(level);
    // recompute the visible cards
    update_cards();
}

const std::vector<ExpressionCard>& ExpressionTree::cards() const {
    // visible cards for the UI
    return m_cards;
}

const std::vector<BreadcrumbEntry>& ExpressionTree::breadcrumb() const {
    // breadcrumb path of the browsed scope
    return m_breadcrumb;
}
