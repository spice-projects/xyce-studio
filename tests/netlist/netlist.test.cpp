#include <gtest/gtest.h>

#include "netlist/netlist.h"

TEST(NetlistParserChecks, parses_title) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title line\nR1 1 0 100\n.END\n");
    // assert
    ASSERT_EQ(topology.m_title, "Title line");
    ASSERT_EQ(topology.m_devices.size(), 1);
    ASSERT_EQ(topology.m_devices[0].m_name, "R1");
}

TEST(NetlistParserChecks, parses_title_directive) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist(".TITLE My Circuit\nR1 1 0 100\n.END\n");
    // assert
    ASSERT_EQ(topology.m_title, "My Circuit");
}

TEST(NetlistParserChecks, handles_continuation_lines) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0\n+ 100\n.END\n");
    // assert
    ASSERT_EQ(topology.m_devices.size(), 1);
    ASSERT_EQ(topology.m_devices[0].m_name, "R1");
    ASSERT_EQ(topology.m_devices[0].m_nodes.size(), 2);
}

TEST(NetlistParserChecks, strips_inline_comments) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100 ; comment\n.END\n");
    // assert
    ASSERT_EQ(topology.m_devices.size(), 1);
    ASSERT_EQ(topology.m_devices[0].m_name, "R1");
}

TEST(NetlistParserChecks, keeps_semicolon_inside_quoted_delimiter) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\n.PRINT TRAN DELIMITER=\";\" V(1)\n.END\n");
    // assert
    ASSERT_EQ(topology.m_directives.size(), 1);
    ASSERT_EQ(topology.m_directives[0], ".PRINT TRAN DELIMITER=\";\" V(1)");
}

TEST(NetlistParserChecks, keeps_semicolon_inside_single_quoted_string) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\n.PRINT TRAN FILE='a;b.csv' V(1)\n.END\n");
    // assert
    ASSERT_EQ(topology.m_directives.size(), 1);
    ASSERT_EQ(topology.m_directives[0], ".PRINT TRAN FILE='a;b.csv' V(1)");
}

TEST(NetlistParserChecks, keeps_semicolon_inside_brace_expression) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 {100 ; tol}\n.END\n");
    // assert
    ASSERT_EQ(topology.m_devices.size(), 1);
    ASSERT_EQ(topology.m_devices[0].m_nodes.size(), 2);
    ASSERT_NE(netlist.find("R1 1 0 {100 ; tol}"), std::string::npos);
}

TEST(NetlistParserChecks, strips_comment_after_quoted_delimiter) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\n.PRINT TRAN DELIMITER=\";\" V(1) ; trailing comment\n.END\n");
    // assert
    ASSERT_EQ(topology.m_directives.size(), 1);
    ASSERT_EQ(topology.m_directives[0], ".PRINT TRAN DELIMITER=\";\" V(1)");
}

TEST(NetlistParserChecks, strips_comment_after_brace_expression) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 {100} ; trailing comment\n.END\n");
    // assert
    ASSERT_EQ(topology.m_devices.size(), 1);
    ASSERT_NE(netlist.find("R1 1 0 {100}"), std::string::npos);
    ASSERT_EQ(netlist.find(";"), std::string::npos);
}

TEST(NetlistParserChecks, extracts_device_nodes) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\nC1 2 0 1u\nQ1 1 2 3 npn\n.END\n");
    // assert
    ASSERT_EQ(topology.m_devices.size(), 3);
    ASSERT_EQ(topology.m_devices[0].m_name, "R1");
    ASSERT_EQ(topology.m_devices[0].m_nodes[0], "1");
    ASSERT_EQ(topology.m_devices[0].m_nodes[1], "0");
    ASSERT_EQ(topology.m_devices[1].m_name, "C1");
    ASSERT_EQ(topology.m_devices[1].m_nodes.size(), 2);
    ASSERT_EQ(topology.m_devices[2].m_name, "Q1");
    ASSERT_EQ(topology.m_devices[2].m_nodes.size(), 3);
}

TEST(NetlistParserChecks, extracts_top_level_nodes) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\nR2 2 1 200\n.END\n");
    // assert
    ASSERT_TRUE(topology.m_nodes.contains("1"));
    ASSERT_TRUE(topology.m_nodes.contains("0"));
    ASSERT_TRUE(topology.m_nodes.contains("2"));
    ASSERT_EQ(topology.m_nodes.size(), 3);
}

TEST(NetlistParserChecks, handles_subcircuits) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\n.SUBCKT INV IN OUT\nM1 OUT IN 0 0 NMOS\n.ENDS INV\nX1 A B INV\n.END\n");
    // assert
    ASSERT_TRUE(topology.m_subcircuit_definitions.contains("INV"));
    ASSERT_EQ(topology.m_subcircuit_definitions.at("INV").m_ports.size(), 2);
    ASSERT_EQ(topology.m_subcircuit_definitions.at("INV").m_devices.size(), 1);
    ASSERT_EQ(topology.m_devices.size(), 1);
    ASSERT_EQ(topology.m_devices[0].m_name, "X1");
}

TEST(NetlistParserChecks, handles_global_nodes) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\n.GLOBAL VDD\nR1 1 VDD 100\n.END\n");
    // assert
    ASSERT_TRUE(topology.m_global_nodes.contains("VDD"));
}

TEST(NetlistParserChecks, handles_dollar_global_nodes) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 $G_VDD 0 100\n.END\n");
    // assert
    ASSERT_TRUE(topology.m_global_nodes.contains("$G_VDD"));
}

TEST(NetlistParserChecks, extracts_simulation_directives) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.OP\n.PRINT DC V(1)\n.TRAN 1u 1m\n.END\n");
    // assert
    ASSERT_GE(topology.m_directives.size(), 2);
    // check that both .OP and .TRAN are present in the extracted directives
    bool has_op = false;
    bool has_tran = false;
    for (const auto& d : topology.m_directives) {
        if (d.find(".OP") == 0)
            has_op = true;
        if (d.find(".TRAN") == 0)
            has_tran = true;
    }
    ASSERT_TRUE(has_op);
    ASSERT_TRUE(has_tran);
}

TEST(NetlistParserChecks, extracts_options_packages) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\n.OPTIONS DEVICE ABSMOS=1e-12\n.OPTIONS TIMEINT MAXORD=2\nR1 1 0 100\n.END\n");
    // assert
    ASSERT_GE(topology.m_directives.size(), 2);
}

TEST(NetlistParserChecks, extracts_fft_options_package) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\n.OPTIONS FFT FFT_ACCURATE=0 FFTOUT=1 FFT_MODE=1\nR1 1 0 100\n.END\n");
    // assert
    bool has_fft = false;
    for (const auto& d : topology.m_directives) {
        if (d.find(".OPTIONS FFT") == 0)
            has_fft = true;
    }
    ASSERT_TRUE(has_fft);
    ASSERT_EQ(netlist.find(".OPTIONS FFT"), std::string::npos);
}

TEST(NetlistParserChecks, extracts_replace_ground_preprocess_directive) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.PREPROCESS REPLACEGROUND TRUE\n.END\n");
    // assert: managed directives are collected separately and leave the sanitized netlist
    ASSERT_EQ(topology.m_directives.size(), 1);
    ASSERT_EQ(topology.m_directives[0], ".PREPROCESS REPLACEGROUND TRUE");
    ASSERT_EQ(netlist.find(".PREPROCESS"), std::string::npos);
}

TEST(NetlistParserChecks, extracts_remove_unused_preprocess_directive) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.PREPROCESS REMOVEUNUSED r,c\n.END\n");
    // assert: the remove-unused statement is managed, not passthrough
    ASSERT_EQ(topology.m_directives.size(), 1);
    ASSERT_EQ(topology.m_directives[0], ".PREPROCESS REMOVEUNUSED r,c");
    ASSERT_EQ(netlist.find(".PREPROCESS"), std::string::npos);
}

TEST(NetlistParserChecks, extracts_add_resistors_preprocess_directives) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.PREPROCESS ADDRESISTORS ONETERMINAL 1G\n.PREPROCESS ADDRESISTORS NODCPATH 10MEG\n.END\n");
    // assert: both qualifier variants are managed and leave the sanitized netlist
    ASSERT_EQ(topology.m_directives.size(), 2);
    ASSERT_EQ(topology.m_directives[0], ".PREPROCESS ADDRESISTORS ONETERMINAL 1G");
    ASSERT_EQ(topology.m_directives[1], ".PREPROCESS ADDRESISTORS NODCPATH 10MEG");
    ASSERT_EQ(netlist.find(".PREPROCESS"), std::string::npos);
}

TEST(NetlistParserChecks, keeps_unknown_preprocess_subcommand_as_passthrough) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.PREPROCESS UNKNOWN TRUE\n.END\n");
    // assert: an unsupported subcommand stays in the sanitized netlist untouched
    ASSERT_TRUE(topology.m_directives.empty());
    ASSERT_NE(netlist.find(".PREPROCESS UNKNOWN TRUE"), std::string::npos);
}

TEST(NetlistParserChecks, extracts_sensitivity_options_package) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.OPTIONS SENSITIVITY direct=1 adjoint=0\n.END\n");
    // assert: the sensitivity package is managed and leaves the sanitized netlist
    ASSERT_EQ(topology.m_directives.size(), 1);
    ASSERT_EQ(topology.m_directives[0], ".OPTIONS SENSITIVITY direct=1 adjoint=0");
    ASSERT_EQ(netlist.find(".OPTIONS SENSITIVITY"), std::string::npos);
}

TEST(NetlistParserChecks, extracts_samples_options_package) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.OPTIONS SAMPLES NUMSAMPLES=10\n.END\n");
    // assert: the sampling package is managed and leaves the sanitized netlist
    ASSERT_EQ(topology.m_directives.size(), 1);
    ASSERT_EQ(topology.m_directives[0], ".OPTIONS SAMPLES NUMSAMPLES=10");
    ASSERT_EQ(netlist.find(".OPTIONS SAMPLES"), std::string::npos);
}

TEST(NetlistParserChecks, extracts_embeddedsamples_options_package) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.OPTIONS EMBEDDEDSAMPLES NUMSAMPLES=5\n.END\n");
    // assert: the embedded sampling package is managed and leaves the sanitized netlist
    ASSERT_EQ(topology.m_directives.size(), 1);
    ASSERT_EQ(topology.m_directives[0], ".OPTIONS EMBEDDEDSAMPLES NUMSAMPLES=5");
    ASSERT_EQ(netlist.find(".OPTIONS EMBEDDEDSAMPLES"), std::string::npos);
}

TEST(NetlistParserChecks, extracts_transient_solver_option_packages) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.OPTIONS NONLIN-TRAN NOX=1\n.OPTIONS LINSOL-AC TYPE=KLU\n.END\n");
    // assert: both transient solver packages are managed and leave the sanitized netlist
    ASSERT_EQ(topology.m_directives.size(), 2);
    ASSERT_EQ(topology.m_directives[0], ".OPTIONS NONLIN-TRAN NOX=1");
    ASSERT_EQ(topology.m_directives[1], ".OPTIONS LINSOL-AC TYPE=KLU");
    ASSERT_EQ(netlist.find(".OPTIONS"), std::string::npos);
}

TEST(NetlistParserChecks, extracts_utility_option_packages) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.OPTIONS DIAGNOSTIC DEBUGLEVEL=3\n.OPTIONS PARSER SCALE=1\n.OPTIONS LOCA MAXSTEPS=5\n.OPTIONS DIST STRATEGY=2\n.END\n");
    // assert: all four utility packages are managed and leave the sanitized netlist
    ASSERT_EQ(topology.m_directives.size(), 4);
    ASSERT_EQ(topology.m_directives[0], ".OPTIONS DIAGNOSTIC DEBUGLEVEL=3");
    ASSERT_EQ(topology.m_directives[1], ".OPTIONS PARSER SCALE=1");
    ASSERT_EQ(topology.m_directives[2], ".OPTIONS LOCA MAXSTEPS=5");
    ASSERT_EQ(topology.m_directives[3], ".OPTIONS DIST STRATEGY=2");
    ASSERT_EQ(netlist.find(".OPTIONS"), std::string::npos);
}

TEST(NetlistParserChecks, extracts_output_and_restart_option_packages) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.OPTIONS OUTPUT PRINTHEADER=false\n.OPTIONS RESTART PACK=1\n.END\n");
    // assert: both interval packages are managed and leave the sanitized netlist
    ASSERT_EQ(topology.m_directives.size(), 2);
    ASSERT_EQ(topology.m_directives[0], ".OPTIONS OUTPUT PRINTHEADER=false");
    ASSERT_EQ(topology.m_directives[1], ".OPTIONS RESTART PACK=1");
    ASSERT_EQ(netlist.find(".OPTIONS"), std::string::npos);
}

TEST(NetlistParserChecks, keeps_standalone_options_as_passthrough) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.OPTIONS RELTOL=1e-3 ABSTOL=1e-9\n.END\n");
    // assert: unpackaged options are not RG syntax (RG 6.1.3) and stay in the sanitized netlist untouched
    ASSERT_TRUE(topology.m_directives.empty());
    ASSERT_NE(netlist.find(".OPTIONS RELTOL=1e-3 ABSTOL=1e-9"), std::string::npos);
}

TEST(NetlistParserChecks, keeps_sampling_alias_options_as_passthrough) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.OPTIONS SAMPLING NUMSAMPLES=10\n.END\n");
    // assert: the non-RG package spelling is not managed and stays in the sanitized netlist untouched
    ASSERT_TRUE(topology.m_directives.empty());
    ASSERT_NE(netlist.find(".OPTIONS SAMPLING NUMSAMPLES=10"), std::string::npos);
}

TEST(NetlistParserChecks, keeps_options_inside_subcircuit_as_passthrough) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\n.SUBCKT INV IN OUT\nR1 IN OUT 100\n.OPTIONS DEVICE GMIN=1e-12\n.ENDS INV\nR2 1 0 200\n.END\n");
    // assert: .OPTIONS inside a subcircuit is ignored per RG 2.1.25 and stays inline
    ASSERT_TRUE(topology.m_directives.empty());
    ASSERT_NE(netlist.find(".OPTIONS DEVICE GMIN=1e-12"), std::string::npos);
}

TEST(NetlistParserChecks, handles_end_short_circuit) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nR1 1 0 100\n.END\nextra stuff\n");
    // assert
    ASSERT_EQ(topology.m_devices.size(), 1);
    // content after .END should not appear in the sanitized netlist
    ASSERT_EQ(netlist.find("extra stuff"), std::string::npos);
}

TEST(NetlistParserChecks, extracts_x_subcircuit_nodes_with_params) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nXU1 IN OUT VDD VSS OPAMP PARAMS: GAIN=2\n.END\n");
    // assert
    ASSERT_EQ(topology.m_devices.size(), 1);
    ASSERT_EQ(topology.m_devices[0].m_nodes.size(), 4);
    ASSERT_EQ(topology.m_devices[0].m_nodes[0], "IN");
    ASSERT_EQ(topology.m_devices[0].m_nodes[3], "VSS");
}

TEST(NetlistParserChecks, sanitized_netlist_keeps_comments) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\n* This is a comment\nR1 1 0 100\n.END\n");
    // assert
    ASSERT_NE(netlist.find("* This is a comment"), std::string::npos);
}

TEST(NetlistParserChecks, handles_y_type_devices) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\nYACC!IN 1 2 3 MODEL\nYLIN_PORT 4 5 MODEL2\n.END\n");
    // assert
    bool has_yacc = false;
    for (const auto& dev : topology.m_devices) {
        if (dev.m_name == "YACC!IN") {
            has_yacc = true;
            ASSERT_EQ(dev.m_nodes.size(), 3);
        }
    }
    ASSERT_TRUE(has_yacc);
}

TEST(NetlistParserChecks, extracts_plot_directives_and_removes_them_from_the_body) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\n.PLOT V(1) V(2,N3) abs(I(R1))\n.PLOT I(R3)\n.END\n");
    // assert — every .PLOT line is recorded and lifted out of the body like any other managed directive
    ASSERT_EQ(topology.m_plot_directives.size(), 2);
    ASSERT_EQ(topology.m_plot_directives[0], ".PLOT V(1) V(2,N3) abs(I(R1))");
    ASSERT_EQ(topology.m_plot_directives[1], ".PLOT I(R3)");
    ASSERT_EQ(netlist.find(".PLOT"), std::string::npos);
    // act — the directives are re-inserted with the managed ones by build_final_netlist
    const auto rebuilt = build_final_netlist(netlist, {}, topology.m_plot_directives);
    // assert
    ASSERT_NE(rebuilt.find(".PLOT V(1) V(2,N3) abs(I(R1))\n.PLOT I(R3)\n\n.END"), std::string::npos);
}

TEST(NetlistParserChecks, records_a_plot_directive_verbatim) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\n.plot {V(1) * 2}\n.END\n");
    // assert — the recorded line keeps the authored spelling so the editor reproduces what was written
    ASSERT_EQ(topology.m_plot_directives.size(), 1);
    ASSERT_EQ(topology.m_plot_directives[0], ".plot {V(1) * 2}");
    ASSERT_EQ(netlist.find(".plot"), std::string::npos);
}

TEST(NetlistParserChecks, joins_continuation_lines_of_a_plot_directive) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\n.PLOT V(1)\n+ V(2)\n.END\n");
    // assert — the continuation line joins the directive into a single logical line
    ASSERT_EQ(topology.m_plot_directives.size(), 1);
    ASSERT_EQ(topology.m_plot_directives[0], ".PLOT V(1) V(2)");
    ASSERT_EQ(netlist.find("V(1) V(2)"), std::string::npos);
}

TEST(NetlistParserChecks, plot_directive_without_arguments_is_recorded_verbatim) {
    // arrange / act
    const auto [netlist, topology] = parse_netlist("Title\n.PLOT\n.END\n");
    // assert — an argument-less directive carries no chart and is lifted out of the body like every managed directive
    ASSERT_EQ(topology.m_plot_directives.size(), 1);
    ASSERT_EQ(topology.m_plot_directives[0], ".PLOT");
    ASSERT_EQ(netlist.find(".PLOT"), std::string::npos);
}

TEST(NetlistAssemblyChecks, inserts_directives_before_end) {
    // arrange
    const std::string netlist = "Title line\nR1 1 0 100\n.END\n";
    const std::vector<std::string> directives = {".OP", ".PRINT DC V(1)"};
    // act
    const auto result = build_final_netlist(netlist, directives, {});
    // assert
    ASSERT_NE(result.find(".PRINT DC V(1)\n\n.END"), std::string::npos);
}

TEST(NetlistAssemblyChecks, returns_unchanged_when_no_directives) {
    // arrange
    const std::string netlist = "Title\nR1 1 0 100\n.END\n";
    // act
    const auto result = build_final_netlist(netlist, {}, {});
    // assert
    ASSERT_EQ(result, netlist);
}

TEST(NetlistAssemblyChecks, includes_passthrough_directives) {
    // arrange
    const std::string netlist = "Title\n.END\n";
    const std::vector<std::string> directives = {".OP"};
    const std::vector<std::string> passthrough = {".WIDTH OUT=80"};
    // act
    const auto result = build_final_netlist(netlist, directives, passthrough);
    // assert
    ASSERT_NE(result.find(".WIDTH OUT=80"), std::string::npos);
}

TEST(NetlistAssemblyChecks, inserts_plot_directives_after_the_managed_ones) {
    // arrange
    const std::string netlist = "Title\nR1 1 0 100\n.END\n";
    const std::vector<std::string> directives = {".TRAN 1u 1m", ".PRINT TRAN V(1)"};
    const std::vector<std::string> plots = {".PLOT V(1) abs(I(R1))"};
    // act
    const auto result = build_final_netlist(netlist, directives, plots);
    // assert — the chart declaration follows the output directives it depends on and still sits above .END
    ASSERT_NE(result.find(".PRINT TRAN V(1)\n.PLOT V(1) abs(I(R1))\n\n.END"), std::string::npos);
}

TEST(NetlistAssemblyChecks, returns_a_plot_only_netlist_unchanged_without_directives) {
    // arrange — the parser lifted the .PLOT lines out, so an assembly without them must not re-insert anything
    const std::string netlist = "Title\nR1 1 0 100\n.END\n";
    // act
    const auto result = build_final_netlist(netlist, {}, {});
    // assert
    ASSERT_EQ(result, netlist);
}
