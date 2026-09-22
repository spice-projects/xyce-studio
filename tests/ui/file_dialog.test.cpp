#include <gtest/gtest.h>
#include <string>

#include "ui/file_dialog.h"

TEST(FileDialogChecks, open_file_extensions_lists_every_loadable_format) {
    // arrange / act
    const auto extensions = FileDialog::open_file_extensions();
    // assert — the netlist format plus every output extension the presenter dispatches by in on_open_xyce_file
    EXPECT_EQ(extensions, "cir,raw,prn,csd,csv");
}
