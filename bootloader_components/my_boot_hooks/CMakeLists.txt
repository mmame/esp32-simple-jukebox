idf_component_register(SRCS "hooks.c" REQUIRES hal esp_rom)

# The hooks are weak symbols; hooks.c defines bootloader_hooks_include() so that the linker takes this file along.
