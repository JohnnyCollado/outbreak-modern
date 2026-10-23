#include "MiniTest.h"

#include <cstdlib>
#include <iostream>

void register_input_script_tests();
void register_frame_dump_tests();
void register_diag_tests();
void register_pad_control_tests();

int main()
{
    register_input_script_tests();
    register_frame_dump_tests();
    register_diag_tests();
    register_pad_control_tests();
    const int res = MiniTest::Run();
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(res);
}
