#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// hierarchy classification of an expression name
enum class GroupKind
{
    Flat, // no hierarchy encoded in the name
    Subcircuit, // SPICE subcircuit path separated by colons
    Sheet, // KiCad sheet hierarchy prefixed with slashes
};

// one card of the expression grid: either an expression tile or a
// drill-in scope tile
struct ExpressionCard
{
    // display label; scope-relative for expression cards inside a scope
    std::string label;
    // full expression name for tooltips and the expression builder; empty
    // for scope cards
    std::string full_name;
    // scope kind string: "subcircuit" or "sheet"; empty for expression cards
    std::string kind;
    // signal type (Voltage, Current, ...) for expression cards
    std::string type;
    // scope tile that drills into the group when activated
    bool is_scope = false;
    // expression cards only
    bool selected = false;
    // scope cards only: number of contained expressions
    int count = 0;
    // persistence key of the scope, scope cards only; not shown in the UI
    std::string scope_key;
};

// one segment of the breadcrumb path shown above the grid
struct BreadcrumbEntry
{
    // display label of the scope segment, "All" for the root
    std::string label;
    // scope level restored when the segment is activated
    size_t level = 0;
};

// scope navigation over expression names: subcircuit and sheet scopes
// collapse into single drill-in cards, and a filter flattens every
// matching expression into a plain card list
class ExpressionTree
{
public:
    ExpressionTree() = default;

    ExpressionTree(const ExpressionTree&) = delete;

    ExpressionTree& operator=(const ExpressionTree&) = delete;

    // classification of a single expression name
    [[nodiscard]] static GroupKind kind_of(std::string_view name);

    // hierarchy segments of a name, excluding the trailing leaf segment
    [[nodiscard]] static std::vector<std::string> hierarchy_of(std::string_view name);

    // rebuild the tree from (name, type) pairs and return to the root scope
    void rebuild(const std::vector<std::pair<std::string, std::string>>& items);

    // append a dynamically created expression leaf
    void add_leaf(std::string name, std::string type);

    // true when a leaf with the given full name exists
    [[nodiscard]] bool contains(const std::string& name) const;

    // set the selection flag of a leaf by full name
    void set_selected(const std::string& name, bool selected);

    // full names of the selected leaves in tree order
    [[nodiscard]] std::vector<std::string> selected_names() const;

    // case-insensitive substring filter; empty browses the current scope,
    // non-empty flattens every matching expression into cards
    void set_filter(std::string query);

    // selected-only view; flattens every selected expression into cards,
    // the filter takes precedence while active
    void set_show_selected(bool show);

    // true when the selected-only view is active
    [[nodiscard]] bool show_selected() const;

    // activate the card at the given index: scope cards drill into the
    // group, expression cards toggle their selection
    void activate(size_t card_index);

    // restore the scope stack to the given breadcrumb level, 0 is the root
    void open_breadcrumb(size_t level);

    // visible cards for the UI
    [[nodiscard]] const std::vector<ExpressionCard>& cards() const;

    // breadcrumb path of the browsed scope, empty while filtering
    [[nodiscard]] const std::vector<BreadcrumbEntry>& breadcrumb() const;

private:
    // expression leaf attached to a group or to the root
    struct Leaf
    {
        // full expression name as known to the renderer
        std::string name;
        // signal type label (Voltage, Current, ...)
        std::string type;
        // insertion sequence, reproduces the raw file order when filtering
        size_t order = 0;
        bool selected = false;
    };

    // scope group for a subcircuit or sheet segment
    struct Group
    {
        // display label of the scope segment
        std::string label;
        // persistence key of the scope path
        std::string key;
        GroupKind kind = GroupKind::Subcircuit;
        // child scopes in first-seen order
        std::vector<Group> children;
        // expressions directly attached to this scope
        std::vector<Leaf> leaves;
    };

    // per-card metadata linking a visible card back to its node
    struct CardMeta
    {
        // non-null for expression cards
        Leaf* leaf = nullptr;
        // scope cards only
        std::string scope_key;
        std::string scope_label;
    };

    // split an expression name into its kind and hierarchy segments
    [[nodiscard]] static std::pair<GroupKind, std::vector<std::string>> parse(std::string_view name);

    // search a group subtree for a leaf by full name
    [[nodiscard]] static Leaf* find_leaf(Group& group, const std::string& name);

    // search a group subtree for a leaf by full name
    [[nodiscard]] static const Leaf* find_leaf(const Group& group, const std::string& name);

    // walk a group subtree collecting the names of the selected leaves
    static void collect_selected(const Group& group, std::vector<std::string>& out);

    // walk a group subtree collecting the selected leaves, pre-order
    static void collect_selected_leaves(Group& group, std::vector<Leaf*>& out);

    // collect the leaves of a subtree matching the filter, pre-order
    void collect_matching(Group& group, std::vector<Leaf*>& out);

    // count the descendant leaves of a scope
    [[nodiscard]] int total_count(const Group& group) const;

    // true when the leaf name matches the active filter
    [[nodiscard]] bool matches(const Leaf& leaf) const;

    // insert a leaf into the tree along its hierarchy path
    void insert(Leaf leaf);

    // search a scope subtree for the scope with the given persistence key
    [[nodiscard]] static Group* find_by_key_in(Group& group, const std::string& key);

    // search the tree for the scope with the given persistence key
    [[nodiscard]] Group* find_by_key(const std::string& key);

    // group of the currently browsed scope, the root when at the top
    [[nodiscard]] Group* current_group();

    // emit the expression and scope cards of the current scope
    void emit_scope_cards();

    // emit a flat card list of every expression matching the filter
    void emit_filter_cards();

    // emit a flat card list of every selected expression
    void emit_selected_cards();

    // scope-relative display label of a leaf: the browsed scope prefix is
    // stripped from the probe argument
    [[nodiscard]] std::string display_label(const Leaf& leaf) const;

    // build the breadcrumb path of the browsed scope
    void build_breadcrumb();

    // recompute the visible cards and the breadcrumb
    void update_cards();

    // root pseudo-group holding the flat leaves and the top-level scopes
    Group m_root;
    // insertion counter assigning the leaf order
    size_t m_next_order = 0;
    // browsed scope stack, (key, label) pairs from the root downwards
    std::vector<std::pair<std::string, std::string>> m_scope_stack;
    // active case-insensitive filter, empty shows the scoped cards
    std::string m_filter;
    // selected-only view toggled from the panel link
    bool m_show_selected = false;
    // visible cards for the UI
    std::vector<ExpressionCard> m_cards;
    // metadata parallel to m_cards
    std::vector<CardMeta> m_card_meta;
    // breadcrumb path of the browsed scope
    std::vector<BreadcrumbEntry> m_breadcrumb;
};
