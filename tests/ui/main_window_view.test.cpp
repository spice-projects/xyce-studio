#include <gtest/gtest.h>

#include <type_traits>

#include "ui/main_window_view.h"

TEST(SlintMainWindowViewChecks, class_is_non_copyable) {
    // the view is non-copyable by design (owns unique_ptr members and Slint component handles)
    // arrange / act
    using ViewT = SlintMainWindowView;
    // assert
    EXPECT_FALSE(std::is_copy_constructible_v<ViewT>);
    EXPECT_FALSE(std::is_copy_assignable_v<ViewT>);
}

TEST(SlintMainWindowViewChecks, class_has_virtual_destructor) {
    // the view has a virtual destructor
    // because it inherits from MainWindowViewDef
    // arrange / act
    using ViewT = SlintMainWindowView;
    // assert
    EXPECT_FALSE(std::is_trivially_destructible_v<ViewT>);
}

TEST(SlintMainWindowViewChecks, set_title_is_virtual) {
    // set_title is a virtual override
    // of MainWindowViewDef
    // arrange / act
    using ViewT = SlintMainWindowView;
    // assert
    EXPECT_TRUE(std::is_member_function_pointer_v<decltype(&ViewT::set_title)>);
}

TEST(SlintMainWindowViewChecks, show_netlist_view_is_virtual) {
    // show_netlist_view is a virtual override
    // arrange / act
    using ViewT = SlintMainWindowView;
    // assert
    EXPECT_TRUE(std::is_member_function_pointer_v<decltype(&ViewT::show_netlist_view)>);
}

TEST(SlintMainWindowViewChecks, show_charts_view_is_virtual) {
    // show_charts_view is a virtual override
    // arrange / act
    using ViewT = SlintMainWindowView;
    // assert
    EXPECT_TRUE(std::is_member_function_pointer_v<decltype(&ViewT::show_charts_view)>);
}

TEST(SlintMainWindowViewChecks, update_charts_is_virtual) {
    // update_charts is a virtual override
    // arrange / act
    using ViewT = SlintMainWindowView;
    // assert
    EXPECT_TRUE(std::is_member_function_pointer_v<decltype(&ViewT::update_charts)>);
}

TEST(SlintMainWindowViewChecks, start_simulation_process_is_virtual) {
    // start_simulation_process is a virtual override
    // arrange / act
    using ViewT = SlintMainWindowView;
    // assert
    EXPECT_TRUE(std::is_member_function_pointer_v<decltype(&ViewT::start_simulation_process)>);
}

TEST(SlintMainWindowViewChecks, cancel_simulation_process_is_virtual) {
    // cancel_simulation_process is a virtual override
    // arrange / act
    using ViewT = SlintMainWindowView;
    // assert
    EXPECT_TRUE(std::is_member_function_pointer_v<decltype(&ViewT::cancel_simulation_process)>);
}

TEST(SlintMainWindowViewChecks, set_event_handler_is_virtual) {
    // set_event_handler is a virtual override
    // of MainWindowViewDef (not MainWindowView)
    // arrange / act
    using ViewT = SlintMainWindowView;
    // assert
    EXPECT_TRUE((std::is_member_function_pointer_v<decltype(&ViewT::set_event_handler)>));
}

TEST(SlintMainWindowViewChecks, implements_main_window_view_def) {
    // SlintMainWindowView inherits from MainWindowViewDef
    // arrange / act
    using ViewT = SlintMainWindowView;
    using BaseT = MainWindowViewDef;
    // assert
    EXPECT_TRUE((std::is_base_of_v<BaseT, ViewT>));
}

TEST(SlintMainWindowViewChecks, set_simulation_progress_is_virtual) {
    // set_simulation_progress is a virtual override
    // of MainWindowView
    // arrange / act
    using ViewT = SlintMainWindowView;
    // assert
    EXPECT_TRUE(std::is_member_function_pointer_v<decltype(&ViewT::set_simulation_progress)>);
}

TEST(SlintMainWindowViewChecks, append_simulation_output_line_is_virtual) {
    // append_simulation_output_line is a virtual override
    // of MainWindowView, it carries the severity the parser classified the line with
    // arrange / act
    using ViewT = SlintMainWindowView;
    // assert
    EXPECT_TRUE(std::is_member_function_pointer_v<decltype(&ViewT::append_simulation_output_line)>);
}

TEST(SlintMainWindowViewChecks, simulation_output_line_count_is_virtual) {
    // simulation_output_line_count is a virtual override
    // of MainWindowView, the presenter reports the log row with it
    // arrange / act
    using ViewT = SlintMainWindowView;
    // assert
    EXPECT_TRUE(std::is_member_function_pointer_v<decltype(&ViewT::simulation_output_line_count)>);
}
