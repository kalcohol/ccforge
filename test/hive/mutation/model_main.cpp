// Standalone entry point used only by verify_hive_mutation.cmake.  Keeping it
// independent of GoogleTest lets the script compile the exact same model
// against a copied, text-mutated implementation without altering source files.
#include "hive_model.hpp"

#include <exception>
#include <iostream>
#include <string>

int main() {
    try {
        std::string error;
        if (!forge_hive_model_test::run(0x5eed1234u, 450, &error)) {
            std::cerr << error << '\n';
            return 1;
        }
        std::cout << "independent hive model accepted the implementation\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "model runner unexpected exception: " << exception.what() << '\n';
        return 2;
    } catch (...) {
        std::cerr << "model runner unexpected non-standard exception\n";
        return 3;
    }
}
