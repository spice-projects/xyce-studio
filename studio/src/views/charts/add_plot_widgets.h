#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "albor/widgets/widget.h"
#include "charts/expression_colors.h"
#include "charts/expression_tree.h"

namespace albor
{
    class TextEngine;
}

namespace studio
{
    // one selectable expression chip the plot dialog grid shows
    class ExpressionChip final : public albor::Widget
    {
    public:
        // build a chip over the engines the dialog uses
        ExpressionChip(albor::BindingEngine* engine, albor::TextEngine* text, std::string label, std::string name, albor::Color dot_color, bool selected, std::string explicit_id);

        // replace the selected state, which repaints the chip
        void set_selected(bool selected);

        // report whether the chip is selected
        [[nodiscard]] bool selected() const;

        // install the work a press performs
        void set_on_clicked(std::function<void()> handler);

        // override: a chip takes the offered cell width and its fixed height
        void measure(const albor::SizeConstraints& constraints, albor::Size& out_size) override;

        // override: assign the cell bounds and fit the label into them
        void arrange(const albor::Rect& bounds) override;

        // override: record the chip plate, the type dot, the label and the check
        void paint_content(albor::RenderContext& context) override;

        // override: hover the chip and activate it on a primary press
        void dispatch_event(albor::Event& event) override;

        // override: announce the chip as a checkable expression row
        [[nodiscard]] albor::SemanticNode semantic_node() const override;

    private:
        // fit the label into the width the arrange pass left for it
        void fit_label();

        // the text engine the label shapes through
        albor::TextEngine* m_text;

        // the label the chip shows
        std::string m_label;

        // the full expression name the tooltip and the builder use
        std::string m_name;

        // the identity dot color of the expression unit
        albor::Color m_dot_color;

        // whether the chart plots the expression
        bool m_selected = false;

        // the press work the dialog installs
        std::function<void()> m_on_clicked;

        // the shaped label the paint pass draws
        albor::TextLayout m_layout;
    };

    // one drill-in scope chip the plot dialog grid shows
    class ScopeChip final : public albor::Widget
    {
    public:
        // build a scope chip over the engines the dialog uses
        ScopeChip(albor::BindingEngine* engine, albor::TextEngine* text, std::string label, std::string kind, int count, std::string explicit_id);

        // install the work a press performs
        void set_on_clicked(std::function<void()> handler);

        // override: a scope chip takes the offered cell width and its fixed height
        void measure(const albor::SizeConstraints& constraints, albor::Size& out_size) override;

        // override: assign the cell bounds and fit the label into them
        void arrange(const albor::Rect& bounds) override;

        // override: record the scoped plate, the kind dot, the label, the count and the chevron
        void paint_content(albor::RenderContext& context) override;

        // override: hover the chip and drill in on a primary press
        void dispatch_event(albor::Event& event) override;

        // override: announce the chip as a drill-in button
        [[nodiscard]] albor::SemanticNode semantic_node() const override;

    private:
        // fit the label into the width the count and chevron leave
        void fit_label();

        // the text engine the label shapes through
        albor::TextEngine* m_text;

        // the scope label the chip shows
        std::string m_label;

        // the scope kind the dot color resolves from
        std::string m_kind;

        // how many expressions the scope holds
        int m_count = 0;

        // the press work the dialog installs
        std::function<void()> m_on_clicked;

        // the shaped label the paint pass draws
        albor::TextLayout m_layout;

        // the shaped count the paint pass draws
        albor::TextLayout m_count_layout;
    };

    // the breadcrumb path and the selected-only toggle above the plot grid
    class PlotBreadcrumb final : public albor::Widget
    {
    public:
        // build the bar over the engines the dialog uses
        PlotBreadcrumb(albor::BindingEngine* engine, albor::TextEngine* text);

        // replace the segments and the selected-only state
        void set_entries(std::vector<BreadcrumbEntry> entries, bool show_selected);

        // install the work a segment press performs
        void set_on_segment_clicked(std::function<void(std::size_t)> handler);

        // install the work the selected-only press performs
        void set_on_selected_clicked(std::function<void()> handler);

        // override: the bar takes the offered width and a fixed height
        void measure(const albor::SizeConstraints& constraints, albor::Size& out_size) override;

        // override: assign the bounds and lay the segments out
        void arrange(const albor::Rect& bounds) override;

        // override: record the segments, the separators and the selected-only link
        void paint_content(albor::RenderContext& context) override;

        // override: activate the segment or the selected-only link under the press
        void dispatch_event(albor::Event& event) override;

        // override: announce the bar and its actions to the automation
        [[nodiscard]] albor::SemanticNode semantic_node() const override;

    private:
        // one laid out segment with the hit rect the press uses
        struct Segment
        {
            // the segment label
            std::string label;

            // the level the segment restores
            std::size_t level = 0;

            // the segment hit rectangle
            albor::Rect bounds;

            // the shaped label the paint pass draws
            albor::TextLayout layout;
        };

        // lay the segments and the selected-only link out inside the arranged bounds
        void layout_segments();

        // the text engine the labels shape through
        albor::TextEngine* m_text;

        // the segments the bar shows
        std::vector<Segment> m_segments;

        // the selected-only link hit rectangle
        albor::Rect m_selected_bounds;

        // the shaped selected-only label
        albor::TextLayout m_selected_layout;

        // whether the selected-only view is active
        bool m_show_selected = false;

        // the segment press work the dialog installs
        std::function<void(std::size_t)> m_on_segment_clicked;

        // the selected-only press work the dialog installs
        std::function<void()> m_on_selected_clicked;
    };

    // the unit and scope legend under the plot grid
    class PlotLegend final : public albor::Widget
    {
    public:
        // build the legend over the engines the dialog uses
        PlotLegend(albor::BindingEngine* engine, albor::TextEngine* text);

        // replace the legend rows
        void set_entries(std::vector<expression_colors::LegendEntry> entries);

        // override: the legend takes the offered width and a fixed height
        void measure(const albor::SizeConstraints& constraints, albor::Size& out_size) override;

        // override: record the dots and labels in one row
        void paint_content(albor::RenderContext& context) override;

        // override: announce the legend as one group
        [[nodiscard]] albor::SemanticNode semantic_node() const override;

    private:
        // the text engine the labels shape through
        albor::TextEngine* m_text;

        // the rows the legend shows
        std::vector<expression_colors::LegendEntry> m_entries;

        // the shaped labels the paint pass draws
        std::vector<albor::TextLayout> m_layouts;
    };
} // namespace studio
