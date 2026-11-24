# set minimum version
cmake_minimum_required(VERSION 3.21)

project(wesnoth)

include(CheckCXXCompilerFlag)
include(CTest)

# use our own version of Find* scripts
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# -----------------------------------------------------------------------
# N64 Build — early-exit and hand off to n64-specific configuration
# -----------------------------------------------------------------------
if(N64_BUILD)
    message(STATUS "=== Wesnoth N64 Port Build ===")
    message(STATUS "Toolchain : ${CMAKE_C_COMPILER}")
    message(STATUS "Target    : Nintendo 64 (VR4300 MIPS, libdragon)")

    # Force-disable everything incompatible with N64
    set(ENABLE_SERVER          OFF CACHE BOOL "" FORCE)
    set(ENABLE_CAMPAIGN_SERVER OFF CACHE BOOL "" FORCE)
    set(ENABLE_NLS             OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTS           OFF CACHE BOOL "" FORCE)
    set(ENABLE_GAME            ON  CACHE BOOL "" FORCE)

    # N64 game target
    file(GLOB_RECURSE N64_PLATFORM_SOURCES "src/n64/*.cpp" "src/n64/*.c")
    # app/, smoke/ and pager/ hold separate programs and the paging runtime;
    # they must not be swept into the engine (each defines its own main()).
    list(FILTER N64_PLATFORM_SOURCES EXCLUDE REGEX "/src/n64/(app|smoke|pager|sdl2)/")
    # SDL 2 and SDL_image built for the console (src/n64/sdl2 + upstream sources).
    enable_language(ASM)
    include(${CMAKE_SOURCE_DIR}/cmake/n64_sdl2.cmake)
    # FreeType, HarfBuzz, pixman, cairo, and the Pango API on top of them.
    include(${CMAKE_SOURCE_DIR}/cmake/n64_text.cmake)
    # Superseded by the real src/game_launcher.cpp.
    list(FILTER N64_PLATFORM_SOURCES EXCLUDE REGEX "/src/n64/n64_game_launcher\\.cpp$")
    # Superseded by the real src/filesystem.cpp running over n64_vfs.cpp.
    list(FILTER N64_PLATFORM_SOURCES EXCLUDE REGEX "/src/n64/n64_filesystem\\.cpp$")
    # Stand-ins for engine code: built separately with every definition made
    # weak (see wesnoth_n64_stubs below), so real engine code always wins.
    set(N64_STUB_SOURCES
        ${CMAKE_SOURCE_DIR}/src/n64/n64_stubs.cpp
        ${CMAKE_SOURCE_DIR}/src/n64/n64_gui2_dialog_impls.cpp
        ${CMAKE_SOURCE_DIR}/src/n64/n64_gui2_stubs.cpp
        ${CMAKE_SOURCE_DIR}/src/n64/n64_draw_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/n64/n64_gui2_dialog_stubs.cpp)
    list(REMOVE_ITEM N64_PLATFORM_SOURCES ${N64_STUB_SOURCES})
    # Superseded by the engine's own font/text.cpp on the Pango subset (src/n64/pango).
    list(FILTER N64_PLATFORM_SOURCES EXCLUDE REGEX "/src/n64/n64_font_text\\.cpp$")
    # Superseded by the real src/preferences/preferences.cpp (file in /save).
    list(FILTER N64_PLATFORM_SOURCES EXCLUDE REGEX "/src/n64/n64_preferences\\.cpp$")
    # Superseded by the real src/events.cpp (controller input arrives as SDL events).
    list(FILTER N64_PLATFORM_SOURCES EXCLUDE REGEX "/src/n64/n64_events\\.cpp$")
    # Superseded by the real SDL 2 port (src/n64/sdl2): the engine's own
    # video.cpp, draw.cpp, picture.cpp, cursor.cpp, sdl/window.cpp, display and
    # unit animation code run on it. The old stand-in renderer, GUI and input
    # layer are no longer part of the engine.
    list(FILTER N64_PLATFORM_SOURCES EXCLUDE REGEX
        "/src/n64/(n64_video_adapter|n64_sdl_wrappers|n64_video|n64_gui|n64_input|n64_input_hotkeys|n64_display|n64_unit_display|n64_combat_display|n64_audio)\\.cpp$")
    # Stand-ins from before the real engine ran (fixed memory pools, binary
    # WML loader, Lua pool allocator, cartridge save chips): unused now, and
    # their static buffers took physical RAM the RDP texture cache needs.
    list(FILTER N64_PLATFORM_SOURCES EXCLUDE REGEX
        "/src/n64/(n64_memory|n64_binary_wml|n64_lua_alloc|n64_save)\\.cpp$")

    # Core game logic sources — comprehensive list for a playable N64 build
    # Grouped by subsystem for clarity. N64 platform layer is auto-globbed above.
    set(N64_GAME_SOURCES
        ${N64_PLATFORM_SOURCES}

        # --- Actions (game commands: move, attack, recruit, etc.) ---
        src/actions/attack.cpp
        src/actions/advancement.cpp
        src/actions/create.cpp
        src/actions/heal.cpp
        src/actions/move.cpp
        src/actions/unit_creator.cpp
        src/actions/undo.cpp
        src/actions/undo_action.cpp
        src/actions/undo_recruit_action.cpp
        src/actions/undo_recall_action.cpp
        src/actions/undo_move_action.cpp
        src/actions/undo_dismiss_action.cpp
        src/actions/undo_update_shroud_action.cpp
        src/actions/vision.cpp
        src/actions/shroud_clearing_action.cpp

        # --- AI ---
        src/ai/manager.cpp
        src/ai/composite/ai.cpp
        src/ai/composite/aspect.cpp
        src/ai/composite/engine.cpp
        src/ai/composite/goal.cpp
        src/ai/composite/stage.cpp
        src/ai/default/contexts.cpp
        src/ai/default/stage_rca.cpp
        src/ai/lua/core.cpp
        src/ai/lua/engine_lua.cpp

        # --- Core game data structures ---
        src/config.cpp
        src/config_cache.cpp
        src/color.cpp
        src/color_range.cpp
        src/tstring.cpp
        src/variable.cpp
        src/variable_info.cpp

        # --- Formula engine ---
        src/formula/formula.cpp
        src/formula/function.cpp
        src/formula/string_utils.cpp
        src/formula/callable_objects.cpp

        # --- Game board and state ---
        src/game_board.cpp
        src/game_config.cpp
        src/game_config_manager.cpp
        src/game_config_view.cpp
        src/game_state.cpp
        src/game_data.cpp

        # --- Game events (WML event system) ---
        src/game_events/action_wml.cpp
        src/game_events/conditional_wml.cpp
        src/game_events/entity_location.cpp
        src/game_events/handlers.cpp
        src/game_events/manager.cpp
        src/game_events/pump.cpp
        src/game_events/wmi_manager.cpp

        # --- Game initialization ---
        src/game_initialization/playcampaign.cpp
        src/game_initialization/connect_engine.cpp
        src/game_initialization/create_engine.cpp
        src/game_initialization/singleplayer.cpp
        src/game_initialization/depcheck.cpp

        # --- Game controllers ---
        src/play_controller.cpp
        src/playsingle_controller.cpp
        src/playmp_controller.cpp
        src/controller_base.cpp
        src/mouse_handler_base.cpp
        src/game_end_exceptions.cpp

        # --- Display (N64 overrides in n64_display.cpp, n64_video_adapter.cpp, n64_draw_manager.cpp) ---
        src/display.cpp
        src/game_display.cpp
        src/display_context.cpp
        src/reports.cpp
        src/theme.cpp
        src/animated.cpp
        src/arrow.cpp

        # --- Map ---
        src/map/map.cpp
        src/map/label.cpp
        src/map/location.cpp
        src/generators/map_generator.cpp
        src/generators/map_create.cpp
        src/generators/default_map_generator.cpp
        src/generators/default_map_generator_job.cpp
        src/generators/lua_map_generator.cpp
        src/generators/cave_map_generator.cpp

        # --- Pathfinding ---
        src/pathfind/pathfind.cpp
        src/pathfind/teleport.cpp
        src/pathfind/astarsearch.cpp

        # --- RNG ---
        src/random.cpp
        src/random_synced.cpp
        src/random_deterministic.cpp
        src/synced_commands.cpp
        src/synced_context.cpp
        src/replay.cpp
        src/replay_helper.cpp

        # --- Scripting (Lua) ---
        src/scripting/game_lua_kernel.cpp
        src/scripting/application_lua_kernel.cpp
        src/scripting/lua_common.cpp
        src/scripting/lua_unit.cpp
        src/scripting/lua_wml.cpp
        src/scripting/lua_race.cpp
        src/scripting/lua_team.cpp
        src/scripting/lua_gui2.cpp
        src/scripting/lua_map_location_ops.cpp
        src/scripting/lua_terrainfilter.cpp
        src/scripting/lua_kernel_base.cpp
        src/scripting/lua_terrainmap.cpp
        src/scripting/lua_cpp_function.cpp
        src/scripting/lua_formula_bridge.cpp
        src/scripting/lua_unit_attacks.cpp
        src/scripting/lua_audio.cpp
        src/scripting/lua_preferences.cpp
        src/scripting/lua_fileops.cpp

        # --- Serialization ---
        src/serialization/binary_or_text.cpp
        src/serialization/parser.cpp
        src/serialization/preprocessor.cpp
        src/serialization/string_utils.cpp
        src/serialization/unicode.cpp
        src/serialization/schema_validator.cpp

        # --- Terrain ---
        src/terrain/builder.cpp
        src/terrain/terrain.cpp
        src/terrain/type_data.cpp
        src/terrain/filter.cpp
        src/terrain/translation.cpp

        # --- Time of day ---
        src/tod_manager.cpp
        src/time_of_day.cpp

        # --- Units ---
        src/units/abilities.cpp
        src/units/animation.cpp
        src/units/attack_type.cpp
        src/units/filter.cpp
        src/units/id.cpp
        src/units/map.cpp
        src/units/types.cpp
        src/units/unit.cpp
        src/units/race.cpp
        src/units/udisplay.cpp
        src/units/drawer.cpp
        src/units/helper.cpp
        src/units/frame.cpp
        src/units/orb_status.cpp

        # --- Team ---
        src/team.cpp
        src/recall_list_manager.cpp
        src/side_filter.cpp

        # --- Savegame ---
        src/savegame.cpp
        src/saved_game.cpp
        src/save_index.cpp
        src/carryover.cpp

        # --- Story screens ---
        src/storyscreen/controller.cpp
        src/storyscreen/part.cpp
        src/storyscreen/parser.cpp
        # storyscreen/render.cpp does not exist in this version; rendering handled by n64_gui

        # --- Other essential modules ---
        src/log.cpp
        src/attack_prediction.cpp
        src/about.cpp
        src/achievements.cpp
        src/picture.cpp
        src/minimap.cpp
        src/persist_context.cpp
        src/persist_manager.cpp
        src/persist_var.cpp
        src/statistics.cpp
        src/chat_events.cpp
        src/chat_command_handler.cpp
        src/chat_log.cpp
        src/menu_events.cpp
        src/mouse_events.cpp
        src/hotkey/hotkey_command.cpp
        src/hotkey/hotkey_item.cpp
        src/hotkey/hotkey_manager.cpp
        src/hotkey/hotkey_handler.cpp
        src/hotkey/command_executor.cpp
        src/tooltips.cpp
        src/quit_confirmation.cpp
        src/sound.cpp
        src/sound_music_track.cpp

        # --- Resources and global state ---
        src/resources.cpp
        src/wml_exception.cpp
        src/display_chat_manager.cpp
        src/game_version.cpp
        src/fake_unit_manager.cpp
        src/fake_unit_ptr.cpp
        src/commandline_options.cpp
        # Wesnoth's real start-up path (entered from src/n64/n64_main.cpp)
        src/wesnoth.cpp
        src/game_launcher.cpp
        src/commandline_argv.cpp
        # Map editor (part of the game)
        src/editor/action/action.cpp
        src/editor/action/action_item.cpp
        src/editor/action/action_label.cpp
        src/editor/action/action_select.cpp
        src/editor/action/action_unit.cpp
        src/editor/action/action_village.cpp
        src/editor/action/mouse/mouse_action.cpp
        src/editor/action/mouse/mouse_action_item.cpp
        src/editor/action/mouse/mouse_action_map_label.cpp
        src/editor/action/mouse/mouse_action_select.cpp
        src/editor/action/mouse/mouse_action_unit.cpp
        src/editor/action/mouse/mouse_action_village.cpp
        src/editor/controller/editor_controller.cpp
        src/editor/editor_display.cpp
        src/editor/editor_main.cpp
        src/editor/map/context_manager.cpp
        src/editor/map/editor_map.cpp
        src/editor/map/map_context.cpp
        src/editor/map/map_fragment.cpp
        src/editor/palette/editor_palettes.cpp
        src/editor/palette/item_palette.cpp
        src/editor/palette/location_palette.cpp
        src/editor/palette/palette_manager.cpp
        src/editor/palette/terrain_palettes.cpp
        src/editor/palette/tristate_button.cpp
        src/editor/palette/unit_palette.cpp
        src/editor/toolkit/brush.cpp
        src/editor/toolkit/editor_toolkit.cpp
        src/utils/spritesheet_generator.cpp
        src/events.cpp
        src/filesystem_sdl.cpp
        src/preferences/preferences.cpp
        src/game_classification.cpp
        src/mp_game_settings.cpp
        src/language.cpp
        src/hash.cpp
        src/build_info.cpp
        src/mp_ui_alerts.cpp
        src/synced_checkup.cpp
        src/image_modifications.cpp
        src/utils/general.cpp
        src/utils/config_filters.cpp
        src/utils/markov_generator.cpp
        src/utils/name_generator_factory.cpp
        src/utils/context_free_grammar_generator.cpp
        src/utils/irdya_datetime.cpp
        src/font/attributes.cpp

        # --- Units (additional) ---
        src/units/animation_component.cpp

        # --- AI (additional) ---
        src/ai/contexts.cpp
        src/ai/composite/rca.cpp
        src/ai/default/ca.cpp
        src/ai/default/recruitment.cpp
        src/ai/default/aspect_attacks.cpp
        src/ai/game_info.cpp
        src/ai/configuration.cpp
        src/ai/actions.cpp
        src/ai/registry.cpp
        src/ai/simulated_actions.cpp
        src/ai/gamestate_observer.cpp
        src/ai/composite/component.cpp
        src/ai/composite/contexts.cpp
        src/ai/default/engine_cpp.cpp
        src/ai/default/attack.cpp
        src/ai/default/ca_move_to_targets.cpp
        src/ai/lua/aspect_advancements.cpp
        src/ai/lua/lua_object.cpp
        src/ai/testing.cpp

        # --- Help system ---
        src/help/help.cpp
        src/help/help_impl.cpp
        src/help/help_topic_generators.cpp

        # --- Preferences ---
        # src/preferences/preferences.cpp — excluded; replaced by src/n64/n64_preferences.cpp
        # (preferences.cpp includes gui/dialogs/file_dialog.hpp which is not built for N64)

        # --- Multiplayer initialization ---
        src/game_initialization/multiplayer.cpp

        # --- Serialization (additional) ---
        src/serialization/markup.cpp
        src/serialization/tokenizer.cpp
        src/serialization/base64.cpp
        src/serialization/validator.cpp

        # --- Additional modules discovered during integration ---
        src/map_settings.cpp
        src/floating_label.cpp
        src/floating_textbox.cpp
        src/utils/parse_network_address.cpp

        # --- GUI2 framework (widget hierarchy needed for modal_dialog + TLD registration) ---
        src/gui/core/top_level_drawable.cpp
        src/gui/core/log.cpp
        src/gui/core/event/dispatcher.cpp
        src/gui/core/event/handler.cpp
        src/gui/core/static_registry.cpp
        src/gui/widgets/widget.cpp
        src/gui/widgets/styled_widget.cpp
        src/gui/widgets/container_base.cpp
        src/gui/widgets/grid.cpp
        src/gui/widgets/panel.cpp
        src/gui/widgets/window.cpp
        src/gui/widgets/settings.cpp
        src/gui/widgets/helper.cpp
        src/gui/core/timer.cpp
        src/gui/core/canvas.cpp
        src/gui/dialogs/modal_dialog.cpp

        # --- Filesystem abstraction (N64: filesystem.cpp replaced by n64_filesystem.cpp) ---
        # The engine's own filesystem code, over the console VFS (n64_vfs.cpp,
        # n64_bfs_shim.hpp).
        src/filesystem.cpp
        src/filesystem_common.cpp

        # --- Font (Pango calls served by src/n64/pango; fontconfig calls likewise) ---
        src/font/text.cpp
        src/font/font_config.cpp
        src/font/sdl_ttf_compat.cpp
        src/font/standard_colors.cpp

        # --- SDL wrappers (rect/texture/surface compile via shim; events/draw replaced by N64) ---
        # src/events.cpp — excluded; replaced by src/n64/n64_events.cpp
        src/draw.cpp
        src/video.cpp
        src/cursor.cpp
        src/sdl/window.cpp
        src/sdl/rect.cpp
        src/sdl/texture.cpp
        src/sdl/surface.cpp
        src/sdl/utils.cpp
        src/sdl/input.cpp

        # --- Additional sources for linker resolution ---
        src/addon/info.cpp
        src/addon/manager.cpp
        src/addon/state.cpp
        src/addon/validation.cpp
        src/config_attribute_value.cpp
        src/countdown_clock.cpp
        src/deprecation.cpp
        src/desktop/battery_info.cpp
        src/desktop/clipboard.cpp
        src/desktop/notifications.cpp
        src/desktop/open.cpp
        src/desktop/paths.cpp
        src/desktop/version.cpp
        src/draw_manager.cpp
        src/font/constants.cpp
        src/format_time_summary.cpp
        src/formula/debugger.cpp
        src/formula/debugger_fwd.cpp
        src/formula/function_gamestate.cpp
        src/formula/tokenizer.cpp
        src/formula/variant.cpp
        src/formula/variant_value.cpp
        src/game_events/manager_impl.cpp
        src/game_events/menu_item.cpp
        src/game_initialization/flg_manager.cpp
        src/game_initialization/lobby_data.cpp
        src/game_initialization/lobby_info.cpp
        src/game_initialization/mp_game_utils.cpp
        src/generic_event.cpp
        src/gui/auxiliary/iterator/iterator.cpp
        src/gui/auxiliary/iterator/walker_container.cpp
        src/gui/auxiliary/iterator/walker_grid.cpp
        src/gui/auxiliary/iterator/walker_scrollbar_container.cpp
        src/gui/auxiliary/iterator/walker_tree_node.cpp
        src/gui/auxiliary/iterator/walker_widget.cpp
        src/gui/auxiliary/tips.cpp
        src/gui/core/event/distributor.cpp
        src/gui/core/gui_definition.cpp
        src/gui/core/linked_group_definition.cpp
        src/gui/core/placer.cpp
        src/gui/core/placer/horizontal_list.cpp
        src/gui/core/placer/vertical_list.cpp
        src/gui/core/tracked_drawable.cpp
        src/gui/core/widget_definition.cpp
        src/gui/core/window_builder.cpp
        src/gui/core/window_builder/helper.cpp
        src/gui/core/window_builder/instance.cpp
        src/gui/dialogs/achievements_dialog.cpp
        src/gui/dialogs/addon/addon_auth.cpp
        src/gui/dialogs/addon/addon_server_info.cpp
        src/gui/dialogs/addon/connect.cpp
        src/gui/dialogs/addon/install_dependencies.cpp
        src/gui/dialogs/addon/license_prompt.cpp
        src/gui/dialogs/addon/manager.cpp
        src/gui/dialogs/addon/uninstall_list.cpp
        src/gui/dialogs/attack_predictions.cpp
        src/gui/dialogs/campaign_difficulty.cpp
        src/gui/dialogs/campaign_selection.cpp
        src/gui/dialogs/chat_log.cpp
        src/gui/dialogs/core_selection.cpp
        src/gui/dialogs/debug_clock.cpp
        src/gui/dialogs/depcheck_confirm_change.cpp
        src/gui/dialogs/depcheck_select_new.cpp
        src/gui/dialogs/drop_down_menu.cpp
        src/gui/dialogs/edit_label.cpp
        src/gui/dialogs/edit_text.cpp
        src/gui/dialogs/editor/choose_addon.cpp
        src/gui/dialogs/editor/custom_tod.cpp
        src/gui/dialogs/editor/edit_label.cpp
        src/gui/dialogs/editor/edit_pbl.cpp
        src/gui/dialogs/editor/edit_pbl_translation.cpp
        src/gui/dialogs/editor/edit_scenario.cpp
        src/gui/dialogs/editor/edit_side.cpp
        src/gui/dialogs/editor/edit_unit.cpp
        src/gui/dialogs/editor/generate_map.cpp
        src/gui/dialogs/editor/generator_settings.cpp
        src/gui/dialogs/editor/new_map.cpp
        src/gui/dialogs/editor/resize_map.cpp
        src/gui/dialogs/editor/tod_new_schedule.cpp
        src/gui/dialogs/end_credits.cpp
        src/gui/dialogs/file_dialog.cpp
        src/gui/dialogs/file_progress.cpp
        src/gui/dialogs/folder_create.cpp
        src/gui/dialogs/formula_debugger.cpp
        src/gui/dialogs/fps_report.cpp
        src/gui/dialogs/game_cache_options.cpp
        src/gui/dialogs/game_delete.cpp
        src/gui/dialogs/game_load.cpp
        src/gui/dialogs/game_save.cpp
        src/gui/dialogs/game_stats.cpp
        src/gui/dialogs/game_version_dialog.cpp
        src/gui/dialogs/gamestate_inspector.cpp
        src/gui/dialogs/gui_test_dialog.cpp
        src/gui/dialogs/help_browser.cpp
        src/gui/dialogs/hotkey_bind.cpp
        src/gui/dialogs/label_settings.cpp
        src/gui/dialogs/language_selection.cpp
        src/gui/dialogs/loading_screen.cpp
        src/gui/dialogs/log_settings.cpp
        src/gui/dialogs/lua_interpreter.cpp
        src/gui/dialogs/message.cpp
        src/gui/dialogs/migrate_version_selection.cpp
        src/gui/dialogs/modeless_dialog.cpp
        src/gui/dialogs/multiplayer/faction_select.cpp
        src/gui/dialogs/multiplayer/lobby.cpp
        src/gui/dialogs/multiplayer/lobby_player_list_helper.cpp
        src/gui/dialogs/multiplayer/match_history.cpp
        src/gui/dialogs/multiplayer/mp_alerts_options.cpp
        src/gui/dialogs/multiplayer/mp_change_control.cpp
        src/gui/dialogs/multiplayer/mp_connect.cpp
        src/gui/dialogs/multiplayer/mp_create_game.cpp
        src/gui/dialogs/multiplayer/mp_host_game_prompt.cpp
        src/gui/dialogs/multiplayer/mp_join_game.cpp
        src/gui/dialogs/multiplayer/mp_join_game_password_prompt.cpp
        src/gui/dialogs/multiplayer/mp_login.cpp
        src/gui/dialogs/multiplayer/mp_method_selection.cpp
        src/gui/dialogs/multiplayer/mp_options_helper.cpp
        src/gui/dialogs/multiplayer/mp_report.cpp
        src/gui/dialogs/multiplayer/mp_staging.cpp
        src/gui/dialogs/multiplayer/player_info.cpp
        src/gui/dialogs/multiplayer/player_list_helper.cpp
        src/gui/dialogs/multiplayer/synced_choice_wait.cpp
        src/gui/dialogs/network_transmission.cpp
        src/gui/dialogs/outro.cpp
        src/gui/dialogs/preferences_dialog.cpp
        src/gui/dialogs/prompt.cpp
        src/gui/dialogs/reachmap_options.cpp
        src/gui/dialogs/screenshot_notification.cpp
        src/gui/dialogs/select_orb_colors.cpp
        src/gui/dialogs/server_info_dialog.cpp
        src/gui/dialogs/simple_item_selector.cpp
        src/gui/dialogs/sp_options_configure.cpp
        src/gui/dialogs/statistics_dialog.cpp
        src/gui/dialogs/story_viewer.cpp
        src/gui/dialogs/surrender_quit.cpp
        src/gui/dialogs/terrain_layers.cpp
        src/gui/dialogs/theme_list.cpp
        src/gui/dialogs/title_screen.cpp
        src/gui/dialogs/tooltip.cpp
        src/gui/dialogs/transient_message.cpp
        src/gui/dialogs/unit_advance.cpp
        src/gui/dialogs/unit_attack.cpp
        src/gui/dialogs/units_dialog.cpp
        src/gui/dialogs/wml_error.cpp
        src/gui/dialogs/wml_message.cpp
        src/gui/gui.cpp
        src/gui/widgets/addon_list.cpp
        src/gui/widgets/button.cpp
        src/gui/widgets/chatbox.cpp
        src/gui/widgets/combobox.cpp
        src/gui/widgets/debug.cpp
        src/gui/widgets/drawing.cpp
        src/gui/widgets/generator.cpp
        src/gui/widgets/horizontal_scrollbar.cpp
        src/gui/widgets/image.cpp
        src/gui/widgets/label.cpp
        src/gui/widgets/listbox.cpp
        src/gui/widgets/matrix.cpp
        src/gui/widgets/menu_button.cpp
        src/gui/widgets/minimap.cpp
        src/gui/widgets/multi_page.cpp
        src/gui/widgets/multiline_text.cpp
        src/gui/widgets/multimenu_button.cpp
        src/gui/widgets/pane.cpp
        src/gui/widgets/password_box.cpp
        src/gui/widgets/progress_bar.cpp
        src/gui/widgets/repeating_button.cpp
        src/gui/widgets/rich_label.cpp
        src/gui/widgets/scroll_label.cpp
        src/gui/widgets/scroll_text.cpp
        src/gui/widgets/scrollbar.cpp
        src/gui/widgets/scrollbar_container.cpp
        src/gui/widgets/scrollbar_panel.cpp
        src/gui/widgets/size_lock.cpp
        src/gui/widgets/slider.cpp
        src/gui/widgets/slider_base.cpp
        src/gui/widgets/spacer.cpp
        src/gui/widgets/spinner.cpp
        src/gui/widgets/stacked_widget.cpp
        src/gui/widgets/tab_container.cpp
        src/gui/widgets/text_box.cpp
        src/gui/widgets/text_box_base.cpp
        src/gui/widgets/toggle_button.cpp
        src/gui/widgets/toggle_panel.cpp
        src/gui/widgets/tree_view.cpp
        src/gui/widgets/tree_view_node.cpp
        src/gui/widgets/unit_preview_pane.cpp
        src/gui/widgets/vertical_scrollbar.cpp
        src/gui/widgets/viewport.cpp
        src/gui/widgets/widget_helpers.cpp
        src/halo.cpp
        src/hotkey/hotkey_handler_mp.cpp
        src/hotkey/hotkey_handler_sp.cpp
        src/key.cpp
        src/lua_jailbreak_exception.cpp
        src/movetype.cpp
        src/mt_rng.cpp
        src/pathutils.cpp
        src/playturn_network_adapter.cpp
        src/replay_controller.cpp
        src/replay_recorder_base.cpp
        src/save_blocker.cpp
        src/scripting/debug_lua.cpp
        src/scripting/lua_attributes.cpp
        src/scripting/lua_color.cpp
        src/scripting/lua_mathx.cpp
        src/scripting/lua_rng.cpp
        src/scripting/lua_stringx.cpp
        src/scripting/lua_unit_type.cpp
        src/scripting/lua_widget.cpp
        src/scripting/lua_widget_attributes.cpp
        src/scripting/lua_widget_methods.cpp
        src/scripting/mapgen_lua_kernel.cpp
        src/scripting/plugins/context.cpp
        src/scripting/plugins/manager.cpp
        src/sdl/exception.cpp
        src/sdl/point.cpp
        src/seed_rng.cpp
        src/serialization/schema/key.cpp
        src/serialization/schema/tag.cpp
        src/serialization/schema/type.cpp
        src/soundsource.cpp
        src/statistics_record.cpp
        src/synced_user_choice.cpp
        src/teambuilder.cpp
        src/utils/charconv.cpp
        src/whiteboard/action.cpp
        src/whiteboard/attack.cpp
        src/whiteboard/highlighter.cpp
        src/whiteboard/manager.cpp
        src/whiteboard/mapbuilder.cpp
        src/whiteboard/move.cpp
        src/whiteboard/recall.cpp
        src/whiteboard/recruit.cpp
        src/whiteboard/side_actions.cpp
        src/whiteboard/suppose_dead.cpp
        src/whiteboard/utility.cpp
        src/widgets/button.cpp
        src/widgets/scrollarea.cpp
        src/widgets/scrollbar.cpp
        src/widgets/textbox.cpp
        src/widgets/widget.cpp
        src/xBRZ/xbrz.cpp
    )

    # Lua submodule — built as a separate C library (no C++ shims).
    # n64_lua_stubs.c lives outside the submodule so the upstream lua/lua.git
    # checkout stays pristine; it provides empty luaopen_os / luaopen_package
    # bodies since loslib.c / loadlib.c are excluded from the N64 build.
    file(GLOB LUA_SOURCES "src/modules/lua/*.c")
    list(FILTER LUA_SOURCES EXCLUDE REGEX ".*(liolib|loslib|loadlib|lua\\.c|luac\\.c).*")
    list(APPEND LUA_SOURCES "${CMAKE_SOURCE_DIR}/src/n64/n64_lua_stubs.c")

    add_library(wesnoth_n64_lua STATIC ${LUA_SOURCES})
    target_include_directories(wesnoth_n64_lua PRIVATE
        src/modules/lua/
        ${N64_INST}/include
    )
    target_compile_definitions(wesnoth_n64_lua PRIVATE
        N64_BUILD=1
        LUA_32BITS=1
        LUA_USE_LONGJMP=1
        LUA_MAXSTACK=512
        LUAI_MAXCSTACK=200
    )

    # The full-engine port does not fit the console's 8 MiB (ELF ~9.2 MB, plus
    # heap) and is opt-in: `cmake --build . --target wesnoth_n64` with
    # -DN64_ENFORCE_SIZE_LIMIT=OFF. The default ROM is wesnoth_n64_app below.
    # Template functions that libstdc++.a defines (weak/COMDAT) are also emitted
    # by our code; the linker keeps one copy. libstdc++ itself is resident and
    # calls them with direct jumps, so whichever copy is kept must be resident.
    # Generate a linker-script fragment (INCLUDEd by cmake/n64_paged.ld) naming
    # their sections.
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libstdc++.a
                    OUTPUT_VARIABLE N64_LIBSTDCXX OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${TOOLCHAIN_PREFIX}nm ${N64_LIBSTDCXX}
                    OUTPUT_VARIABLE N64_LIBSTDCXX_NM ERROR_QUIET)
    string(REGEX MATCHALL "[0-9a-f]+ W [^\n]+" N64_WEAK_LINES "${N64_LIBSTDCXX_NM}")
    set(N64_WEAK_FRAGMENT "/* generated from ${N64_LIBSTDCXX}: weak functions kept resident */\n")
    set(N64_WEAK_SEEN "")
    foreach(line IN LISTS N64_WEAK_LINES)
        string(REGEX REPLACE "^[0-9a-f]+ W " "" sym "${line}")
        list(APPEND N64_WEAK_SEEN "${sym}")
    endforeach()
    list(REMOVE_DUPLICATES N64_WEAK_SEEN)
    foreach(sym IN LISTS N64_WEAK_SEEN)
        # GCC may place a function in .text.unlikely./.hot./.startup. variants.
        string(APPEND N64_WEAK_FRAGMENT
            "*(.text.${sym} .text.unlikely.${sym} .text.hot.${sym} .text.startup.${sym})\n")
    endforeach()
    file(WRITE ${CMAKE_BINARY_DIR}/n64_resident_weak.ld "${N64_WEAK_FRAGMENT}")

    # Paged programs supply their own __wrap___do_global_ctors (the pager must
    # be installed before any constructor runs). libdragon's do_ctors.o is
    # always linked (crtbegin.o and dlfcn.o need its __register_frame_info),
    # so link paged targets against a copy of libdragon.a with libdragon's
    # version of that one function made weak.
    set(N64_LIBDRAGON_PAGED ${CMAKE_BINARY_DIR}/libdragon_paged.a)
    add_custom_command(OUTPUT ${N64_LIBDRAGON_PAGED}
        COMMAND ${TOOLCHAIN_PREFIX}objcopy --weaken-symbol=__wrap___do_global_ctors
                ${N64_INST}/mips64-elf/lib/libdragon.a ${N64_LIBDRAGON_PAGED}
        DEPENDS ${N64_INST}/mips64-elf/lib/libdragon.a
        COMMENT "libdragon_paged.a (pager-owned constructor runner)")
    add_custom_target(libdragon_paged DEPENDS ${N64_LIBDRAGON_PAGED})

    add_executable(wesnoth_n64 EXCLUDE_FROM_ALL ${N64_GAME_SOURCES})

    target_include_directories(wesnoth_n64 PRIVATE
        src/
        src/n64/
        ${N64_SDL2_INCLUDE_DIR}       # real SDL 2 / SDL_image headers first
        ${CMAKE_SOURCE_DIR}/src/n64/pango/include   # Pango subset + fontconfig calls
        ${N64_TEXT_INCLUDE_DIRS}      # cairo, FreeType, HarfBuzz
        src/n64/sdl2_stubs/
        src/modules/lua/
        ${N64_INST}/include
    )

    target_compile_definitions(wesnoth_n64 PRIVATE
        N64_BUILD=1
        LUA_32BITS=1            # use 32-bit integers/floats in Lua (N64 is 32-bit)
        LUA_USE_LONGJMP=1       # use longjmp for Lua errors (no C++ exceptions on N64)
        LUA_MAXSTACK=512        # reduce Lua stack from default 15000
        LUAI_MAXCSTACK=200      # reduce C stack nesting limit
    )

    # Inject shims: SDL → n64_sdl_shim, Boost → n64_boost_shim, plus Pango/Cairo/threading/network/mixer
    # Only apply to C++ sources; C sources (like n64_libgcc.c) cannot consume C++ headers.
    target_compile_options(wesnoth_n64 PRIVATE
        "SHELL:$<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_SOURCE_DIR}/src/n64/n64_engine_prelude.hpp>"
        "SHELL:$<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_SOURCE_DIR}/src/n64/n64_boost_shim.hpp>"
        "SHELL:$<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_SOURCE_DIR}/src/n64/n64_threading_shim.hpp>"
        "SHELL:$<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_SOURCE_DIR}/src/n64/n64_network_shim.hpp>"
    )

    # Linker script: derived at configure time from libdragon's installed
    # n64.ld so it tracks the toolchain. Changes: discard .eh_frame and
    # .gcc_except_table (libgcc/libdragon archives carry them regardless of
    # -fno-unwind-tables; ~1 MB of RAM we cannot spare), and replace the stock
    # size ASSERT with the same 8 MiB limit under our own message.
    set(N64_STOCK_LD "${N64_INST}/mips64-elf/lib/n64.ld")
    if(NOT EXISTS "${N64_STOCK_LD}")
        message(FATAL_ERROR "libdragon linker script not found: ${N64_STOCK_LD}")
    endif()
    file(READ "${N64_STOCK_LD}" N64_LD_TEXT)
    string(REGEX REPLACE "ASSERT\\([^\n]*\n" "" N64_LD_TEXT "${N64_LD_TEXT}")
    string(REPLACE "    .rodata : {"
"    /DISCARD/ : {
        *(.eh_frame)
        *(.eh_frame.*)
        *(.eh_frame_hdr)
        *(.gcc_except_table)
        *(.gcc_except_table.*)
    }
    __EH_FRAME_BEGIN__ = 0;
    .jcr : { KEEP (*(.jcr)) }

    .rodata : {" N64_LD_TEXT "${N64_LD_TEXT}")
    option(N64_ENFORCE_SIZE_LIMIT "Fail the link when the ELF exceeds the 8 MiB RDRAM budget" ON)
    if(N64_ENFORCE_SIZE_LIMIT)
    string(APPEND N64_LD_TEXT "\nASSERT(end - __text_start <= 8 * 1024 * 1024 - 64 * 1024 - 1024, \"ELF too big for N64 memory (8 MiB with Expansion Pak, minus reserved)\");\n")
    endif()
    file(WRITE "${CMAKE_BINARY_DIR}/wesnoth_n64.ld" "${N64_LD_TEXT}")

    # Linker script — use libdragon's standard n64.ld
    # --allow-multiple-definition: n64 stubs intentionally override real
    # implementations for symbols that won't work on N64. Stubs are linked
    # first (via N64_PLATFORM_SOURCES at the head of the source list), so
    # they take precedence under this flag.
    # The engine is demand-paged: its code, read-only data and exception tables
    # live in ROM and are paged into RAM by src/n64/pager (cmake/n64_paged.ld).
    # The pager provides the constructor runner libdragon's entry point calls.
    target_sources(wesnoth_n64 PRIVATE src/n64/pager/n64_pager.c)
    target_include_directories(wesnoth_n64 PRIVATE src/n64/pager/)
    target_compile_definitions(wesnoth_n64 PRIVATE N64_PAGED=1)
    # Resident code must be real object code (not LTO IR), or its sections
    # would not carry the file names cmake/n64_paged.ld selects on.
    target_sources(wesnoth_n64 PRIVATE src/n64/pager/n64_vheap.cpp src/n64/pager/n64_throwlog.cpp
        src/n64/pager/n64_eh_sext.S)
    # LZ4 compressor for the pager's compressed swap (resident, see n64_paged.ld).
    target_sources(wesnoth_n64 PRIVATE src/n64/pager/lz4/lz4.c)
    set_source_files_properties(src/n64/pager/lz4/lz4.c PROPERTIES COMPILE_OPTIONS "-fno-lto;-O2")
    # Diagnostics: -DN64_HEAP_PROFILE=ON logs the call sites holding the most heap.
    option(N64_HEAP_PROFILE "Heap profile by call site (diagnostics)" OFF)
    if(N64_HEAP_PROFILE)
        set_property(SOURCE src/n64/pager/n64_vheap.cpp APPEND PROPERTY COMPILE_DEFINITIONS "N64_HEAP_PROFILE=1")
    endif()
    # Testing: -DN64_SWAP_AREA_LIMIT_MB=<n> caps the SDRAM swap area (as a bigger ROM would).
    set(N64_SWAP_AREA_LIMIT_MB "" CACHE STRING "Cap the SDRAM swap area (testing)")
    if(N64_SWAP_AREA_LIMIT_MB)
        set_property(SOURCE src/n64/pager/n64_pager.c APPEND PROPERTY
            COMPILE_DEFINITIONS "N64_SWAP_AREA_LIMIT_MB=${N64_SWAP_AREA_LIMIT_MB}u")
    endif()
    set_source_files_properties(src/n64/pager/n64_pager.c src/n64/n64_libgcc.c
        src/n64/pager/n64_vheap.cpp src/n64/pager/n64_throwlog.cpp
        PROPERTIES COMPILE_OPTIONS "-fno-lto")
    # Lua's default allocator (luaL_newstate -> l_alloc in lauxlib.c) uses the
    # swappable heap too.
    set_source_files_properties(src/modules/lua/lauxlib.c PROPERTIES
        COMPILE_DEFINITIONS "realloc=n64_vrealloc;free=n64_vfree")
    # Wesnoth's own main() is entered from src/n64/n64_main.cpp after platform
    # initialisation; rename it so it does not collide with the entry point.
    set_source_files_properties(src/wesnoth.cpp PROPERTIES COMPILE_DEFINITIONS "main=wesnoth_main")
    add_dependencies(wesnoth_n64 libdragon_paged)
    set_target_properties(wesnoth_n64 PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/cmake/n64_paged.ld)
    target_link_options(wesnoth_n64 PRIVATE
        -T ${CMAKE_SOURCE_DIR}/cmake/n64_paged.ld
        -L${CMAKE_BINARY_DIR}
        -Wl,--wrap=__do_global_ctors
        -Wl,--wrap=__cxa_throw
        -Wl,--wrap=gettimeofday
        -Wl,--wrap=_Unwind_Resume
        -Wl,--wrap=__cxa_begin_catch
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_BINARY_DIR}/wesnoth_n64.map
    )

    # N64 stand-ins for engine functionality not yet ported. Every definition
    # is weakened after compilation, so an engine definition of the same
    # symbol always wins and stubs only fill real gaps; adding an engine source
    # retires its stubs automatically. (Previously the link used
    # --allow-multiple-definition, which let stubs silently replace 96 engine
    # functions, including gui2::init and game_config::path.)
    add_library(wesnoth_n64_stubs STATIC ${N64_STUB_SOURCES})
    target_include_directories(wesnoth_n64_stubs PRIVATE $<TARGET_PROPERTY:wesnoth_n64,INCLUDE_DIRECTORIES>)
    target_compile_definitions(wesnoth_n64_stubs PRIVATE $<TARGET_PROPERTY:wesnoth_n64,COMPILE_DEFINITIONS>)
    target_compile_options(wesnoth_n64_stubs PRIVATE $<TARGET_PROPERTY:wesnoth_n64,COMPILE_OPTIONS> -fno-lto)
    add_custom_command(TARGET wesnoth_n64_stubs POST_BUILD
        COMMAND ${TOOLCHAIN_PREFIX}objcopy --weaken $<TARGET_FILE:wesnoth_n64_stubs>
        COMMENT "Weakening N64 stand-in definitions")

    # Build with -Oz (aggressive size opt) + LTO to fit the 8 MiB N64 RAM
    # budget. Wesnoth refuses to compile with NDEBUG (asserts are required),
    # so we only pass -Oz and not -DNDEBUG.
    # -fno-asynchronous-unwind-tables -fno-unwind-tables drop .eh_frame /
    # .gcc_except_table which add ~1MB of unwind data we cannot afford.
    # (Wesnoth uses dynamic_cast so we cannot disable RTTI.)
    # -mlong-calls: paged code (kseg2) calls resident code (kseg0), which is a
    # different 256 MiB jump region. C++ exception tables are kept: the engine
    # relies on exceptions, and with paging they cost ROM, not RAM.
    target_compile_options(wesnoth_n64 PRIVATE
        -Oz -flto=auto -ffat-lto-objects -mlong-calls)
    target_compile_options(wesnoth_n64_lua PRIVATE
        -Oz -flto=auto -ffat-lto-objects -mlong-calls)
    target_link_options(wesnoth_n64 PRIVATE
        -Oz -flto=auto -mlong-calls)

    # Link libdragon + zlib (for save compression)
    target_link_libraries(wesnoth_n64 PRIVATE
        wesnoth_n64_lua
        n64_sdl2_image
        n64_sdl2_mixer
        n64_sdl2
        n64_pango
        n64_cairo
        n64_pixman
        n64_harfbuzz
        n64_freetype
        wesnoth_n64_stubs
        ${N64_LIBDRAGON_PAGED}
        ${N64_INST}/mips64-elf/lib/libdragonsys.a
        # zlib stubbed inline for N64 (no compression, just memcpy)
        m c gcc
    )

    # Platform-layer smoke test: same linker script, ROMFS and runtime code
    # (romfs/memory/video) as the game, none of the engine. Small enough to
    # boot and screenshot on an emulator; see src/n64/smoke/.
    add_executable(wesnoth_n64_smoke
        src/n64/smoke/n64_smoke_main.cpp
        src/n64/n64_romfs.cpp
        src/n64/n64_memory.cpp
        src/n64/n64_video.cpp
    )
    target_include_directories(wesnoth_n64_smoke PRIVATE src/ src/n64/ ${N64_INST}/include)
    target_compile_definitions(wesnoth_n64_smoke PRIVATE N64_BUILD=1)
    target_compile_options(wesnoth_n64_smoke PRIVATE -Oz)
    target_link_options(wesnoth_n64_smoke PRIVATE
        -T ${CMAKE_BINARY_DIR}/wesnoth_n64.ld
        # libdragon runs C++ static constructors through this wrapper (see
        # n64.mk N64_LDFLAGS). Without it no global constructor ever runs --
        # including libdragon's own __inspector_init.
        -Wl,--wrap=__do_global_ctors
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_BINARY_DIR}/wesnoth_n64_smoke.map
    )
    target_link_libraries(wesnoth_n64_smoke PRIVATE
        ${N64_INST}/mips64-elf/lib/libdragon.a
        ${N64_INST}/mips64-elf/lib/libdragonsys.a
        m c gcc
    )

    # The runnable ROM: a native front-end (title, campaign/scenario browser and
    # a real-map hex viewer) built on the port's platform layer and the ROMFS.
    # It does not link the Wesnoth engine, which does not fit the console's RAM.
    add_executable(wesnoth_n64_app
        src/n64/app/n64_app_main.cpp
        src/n64/n64_romfs.cpp
        src/n64/n64_memory.cpp
        src/n64/n64_video.cpp
        src/n64/n64_audio.cpp
        src/n64/n64_input.cpp
        src/n64/n64_gui.cpp
    )
    target_include_directories(wesnoth_n64_app PRIVATE src/ src/n64/ ${N64_INST}/include)
    target_compile_definitions(wesnoth_n64_app PRIVATE N64_BUILD=1)
    target_compile_options(wesnoth_n64_app PRIVATE -Oz)
    target_link_options(wesnoth_n64_app PRIVATE
        -T ${CMAKE_BINARY_DIR}/wesnoth_n64.ld
        -Wl,--wrap=__do_global_ctors
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_BINARY_DIR}/wesnoth_n64_app.map
    )
    target_link_libraries(wesnoth_n64_app PRIVATE
        ${N64_INST}/mips64-elf/lib/libdragon.a
        ${N64_INST}/mips64-elf/lib/libdragonsys.a
        m c gcc
    )


    # Demand-paged variant of the front-end: same program, but its code and
    # read-only data live in ROM and are paged into a small frame pool by
    # src/n64/pager. Built with only 2 frames (32 KiB) so it evicts constantly;
    # it validates the mechanism the full engine needs (its code is > RAM).
    add_executable(wesnoth_n64_app_paged
        src/n64/app/n64_app_main.cpp
        src/n64/n64_romfs.cpp
        src/n64/n64_memory.cpp
        src/n64/n64_video.cpp
        src/n64/n64_audio.cpp
        src/n64/n64_input.cpp
        src/n64/n64_gui.cpp
        src/n64/pager/n64_pager.c
        src/n64/n64_libgcc.c
    )
    target_include_directories(wesnoth_n64_app_paged PRIVATE src/ src/n64/ src/n64/pager/ ${N64_INST}/include)
    target_compile_definitions(wesnoth_n64_app_paged PRIVATE N64_BUILD=1 N64_PAGED=1 N64_PAGER_FRAMES=2)
    target_compile_options(wesnoth_n64_app_paged PRIVATE -Oz -mlong-calls)
    target_link_options(wesnoth_n64_app_paged PRIVATE
        -T ${CMAKE_SOURCE_DIR}/cmake/n64_paged.ld
        -L${CMAKE_BINARY_DIR}
        -Wl,--wrap=__do_global_ctors
        -Wl,--gc-sections
        -Wl,-Map=${CMAKE_BINARY_DIR}/wesnoth_n64_app_paged.map
    )
    add_dependencies(wesnoth_n64_app_paged libdragon_paged)
    target_link_libraries(wesnoth_n64_app_paged PRIVATE
        ${N64_LIBDRAGON_PAGED}
        ${N64_INST}/mips64-elf/lib/libdragonsys.a
        m c gcc
    )
    set_target_properties(wesnoth_n64_app_paged PROPERTIES LINK_DEPENDS ${CMAKE_SOURCE_DIR}/cmake/n64_paged.ld)

    # ROMFS blob built by tools/n64-asset-pipeline (see tools/n64-build-romfs.sh).
    # Without it the ROM still builds, but n64_romfs_init() fails at boot.
    set(WESNOTH_N64_ROMFS "" CACHE FILEPATH "Path to wesnoth.wnrf to append to the ROM")
    n64_create_paged_rom(wesnoth_n64 "${WESNOTH_N64_ROMFS}")
    n64_create_rom(wesnoth_n64_smoke "${WESNOTH_N64_ROMFS}")
    n64_create_rom(wesnoth_n64_app "${WESNOTH_N64_ROMFS}")
    n64_create_paged_rom(wesnoth_n64_app_paged "${WESNOTH_N64_ROMFS}")

    # Asset pipeline tool (host-side, always built with the host compiler)
    # add_subdirectory(tools/n64-asset-pipeline)

    # Stop processing the rest of this file (it's PC-specific)
    return()
endif()
# -----------------------------------------------------------------------
# END N64 BUILD BLOCK
# -----------------------------------------------------------------------

# function to remove a flag from a variable
function(RemoveFlag VAR SCOPE FLAG DOCSTRING)
	if(NOT "${${VAR}}" STREQUAL "")
		MESSAGE("Removing ${FLAG} flag from ${VAR}")
		separate_arguments(${VAR})
		list(REMOVE_ITEM ${VAR} ${FLAG})
		string(REPLACE ";" " " ${VAR} "${${VAR}}")

		if("${SCOPE}" STREQUAL "CACHE")
			set(${VAR} "${${VAR}}" CACHE STRING "${DOCSTRING}" FORCE)
		elseif("${SCOPE}" STREQUAL "SCRIPT")
			set(${VAR} "${${VAR}}" PARENT_SCOPE)
		endif()
	endif()
endfunction()

#
# Options
#

# Adhere to GNU filesystem layout conventions
include(GNUInstallDirs)

#Path options
set(DATADIRNAME "wesnoth" CACHE STRING "change the name of the directory for the read-only architecture-independent game data")
set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}" CACHE STRING "change the dir where binaries are placed right at compile time")
set(LOCALEDIR "translations" CACHE STRING "change the name of the locale data directory to a non-default name")
set(PREFERENCES_DIR "" CACHE STRING "Use a non-default preferences directory (.wesnoth on unix)")
set(DEFAULT_PREFS_FILE "" CACHE STRING "Set system wide preferences file")

#server options
set(SERVER_UID "" CACHE STRING "User id of the user who runs wesnothd")
set(SERVER_GID "" CACHE STRING "Group id of the user who runs wesnothd")
set(FIFO_DIR "/var/run/wesnothd" CACHE STRING "Directory for the wesnothd fifo socket file")

#build options
option(ENABLE_GAME "Enable compilation of the game" ON)
option(ENABLE_CAMPAIGN_SERVER "Enable compilation of campaign(add-ons) server")
option(ENABLE_SERVER "Enable compilation of MP server" ON)
option(ENABLE_MYSQL "Enable building MP/add-ons servers with mysql support" OFF)
option(ENABLE_TESTS "Build unit tests")
option(ENABLE_NLS "Enable building of translations" ${ENABLE_GAME})

# The next line is the minimum version of Boost. The version that's actually found will be put in Boost_VERSION.
set(MIN_USABLE_BOOST_VERSION "1.70")
set(BOOST_REQ_COMPONENTS iostreams program_options regex thread random coroutine locale filesystem graph)

if(ENABLE_TESTS)
  list(APPEND BOOST_REQ_COMPONENTS unit_test_framework)
endif()

find_package(Boost ${MIN_USABLE_BOOST_VERSION} REQUIRED CONFIG COMPONENTS ${BOOST_REQ_COMPONENTS}
                                           OPTIONAL_COMPONENTS charconv process)

if(NOT WIN32)
	set(Lua_FIND_VERSION_MAJOR 5)
	set(Lua_FIND_VERSION_MINOR 4)
	option(ENABLE_SYSTEM_LUA "Enable use of system Lua ${Lua_FIND_VERSION_MAJOR}.${Lua_FIND_VERSION_MINOR} (compiled as C++)" OFF)
endif()
if(ENABLE_SYSTEM_LUA)
	set(Lua_FIND_VERSION_EXACT ON)
	set(Lua_FIND_VERSION_COUNT 2)
	include(FindLua)
	if(NOT LUA_FOUND)
		message(FATAL_ERROR "Lua ${Lua_FIND_VERSION_MAJOR}.${Lua_FIND_VERSION_MINOR} C++ not found. Try setting 'LUA_DIR'.")
	endif()
else()
	if(NOT EXISTS "${CMAKE_SOURCE_DIR}/src/modules/lua/.git")
		message(FATAL_ERROR "Lua submodule does not exist. You must run 'git submodule update --init --recursive' to initialize it.")
	else()
		message("-- Lua submodule found.")
	endif()
endif()

# set what std version to use
if(NOT CXX_STD)
	set(CXX_STD "17")
endif()
set(CMAKE_CXX_STANDARD ${CXX_STD})
# make sure to force using it
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# forbid defaulting to gnu++NN instead of c++NN
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL 1.0 REQUIRED)

if(APPLE)
	find_library(APPKIT_LIBRARY AppKit REQUIRED)
	find_library(FOUNDATION_LIBRARY Foundation REQUIRED)
	find_library(IOKIT_LIBRARY IOKit REQUIRED)
	find_library(SECURITY_LIBRARY Security REQUIRED)
endif()

find_package(ICU REQUIRED COMPONENTS data i18n uc)

# no, gettext executables are not required when NLS is deactivated
find_package(Gettext)
find_package(Python)

find_package(X11)

if(NOT WIN32 AND NOT ENABLE_SYSTEM_LUA)
	# Use the safer `mkstemp' instead of `tmpnam' on POSIX systems.
	add_compile_definitions(LUA_USE_POSIX)
endif()

#check for some compiler/arch specific things and export defines accordingly...
include(SearchForStuff)

# if no build type is specified, it can happen that the game is built without
# optimization (c.f. bug #23445), work around this by enforcing "release" type
# if nothing was selected
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel." FORCE)
	message("No build type specified, defaulting to Release")
endif()

if(NOT DEFINED ENABLE_DISPLAY_REVISION)
	# can't run the shell script on windows
	if(NOT WIN32)
		# Test whether the code is used in a repository if not autorevision will
		# fail and should be disabled by default. If inside a repository enable
		# the display of revision numbers by default.
		execute_process(
			COMMAND ${CMAKE_SOURCE_DIR}/utils/autorevision.sh -t h > ${CMAKE_CURRENT_BINARY_DIR}/revision.dummy
			WORKING_DIRECTORY
				${CMAKE_SOURCE_DIR}
			OUTPUT_VARIABLE
				ENABLE_DISPLAY_REVISION_TEST_OUTPUT
			ERROR_VARIABLE
				ENABLE_DISPLAY_REVISION_TEST_ERRNO
		)

		if("${ENABLE_DISPLAY_REVISION_TEST_ERRNO}" STREQUAL "")
			set(DEFAULT_ENABLE_DISPLAY_REVISION true)
		else()
			set(DEFAULT_ENABLE_DISPLAY_REVISION false)
		endif()

		unset(ENABLE_DISPLAY_REVISION_TEST_OUTPUT)
		unset(ENABLE_DISPLAY_REVISION_TEST_ERRNO)
	else()
		set(DEFAULT_ENABLE_DISPLAY_REVISION false)
	endif()
endif()

option(
	ENABLE_DISPLAY_REVISION
	"Enable the display of the revision number in the game, only enable it when in a checkout"
	${DEFAULT_ENABLE_DISPLAY_REVISION}
)

if(UNIX AND NOT APPLE AND NOT CYGWIN)
	option(ENABLE_DESKTOP_ENTRY "enable installation of desktop entry files" ON)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	option(ENABLE_APPDATA_FILE "enable installation of an appdata file for appstream" ON)
endif()

option(HARDEN "Whether to enable options to harden the executables" ON)
option(ENABLE_STRICT_COMPILATION "Sets the strict compilation mode" OFF)
option(ENABLE_PEDANTIC_COMPILATION "Sets the pedantic compilation mode" OFF)
option(ENABLE_DEBUG_WINDOW_LAYOUT "Add the debug option to allow the generation of debug layout files in dot format" OFF)
option(ENABLE_DESIGN_DOCUMENTS "Enables the generation of design documents, and has additional dependencies" OFF)
option(ENABLE_LTO "Sets Link Time Optimization for Release builds" OFF)
option(GLIBCXX_ASSERTIONS "Whether to define _GLIBCXX_ASSERTIONS" OFF)
option(GLIBCXX_DEBUG "Whether to define _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC. Requires a version of Boost's program_options that's compiled with __GLIBCXX_DEBUG too." OFF)
option(ENABLE_POT_UPDATE_TARGET "Enables the tools to update the pot files and manuals. This target has extra dependencies." OFF)
option(FORCE_COLOR_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." FALSE)
option(CLANG_TIDY "Enable clang-tidy linter checks." OFF)

if(UNIX AND NOT APPLE AND NOT CYGWIN)
	option(ENABLE_NOTIFICATIONS "Enable Window manager notification messages" ON)
endif()

set(BINARY_SUFFIX "" CACHE STRING "Suffix behind all binaries")
set(BINARY_PREFIX "" CACHE STRING "Prefix in front of all binaries")

#
# Handle options (set paths/definitions/etc...)
#

### Set the environment compiler flags.

if(NOT MSVC)
	if(NOT DEFINED CXX_FLAGS_USER)

		MESSAGE(STATUS "Environment compiler flags set to »${CXX_FLAGS_USER}«")
		set(CXX_FLAGS_USER
			"$ENV{CXXFLAGS}"
			CACHE
			STRING
			"The CXXFLAGS environment variable used for the initial generation."
			FORCE
		)

	endif()

	set(COMPILER_FLAGS "-Wall -Wextra -Wno-c++20-extensions -Werror=non-virtual-dtor -Wno-unused-local-typedefs -Wno-maybe-uninitialized -Wold-style-cast -Wtrampolines")

	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		set(COMPILER_FLAGS "${COMPILER_FLAGS} -Qunused-arguments -Wno-unknown-warning-option -Wmismatched-tags -Wno-conditional-uninitialized -Wno-unused-lambda-capture")
	endif()

	if(CMAKE_CXX_COMPILER_ID MATCHES "GNU"
			AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13
			AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
		# GCC-13 added this new warning, and included it in -Wextra,
		# however in GCC-13 it has a lot of false positives.
		#
		# It's likely to generate false postives with GCC-14 too, but
		# I'm using a narrow version check as GCC-14 is still in dev.
		# See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=110075
		set(COMPILER_FLAGS "${COMPILER_FLAGS} -Wno-dangling-reference")
	endif()

### Set strict compiler flags.

	if(ENABLE_STRICT_COMPILATION)
		set(COMPILER_FLAGS "${COMPILER_FLAGS} -Werror")
	endif()

### Set pedantic compiler flags.

	if(ENABLE_PEDANTIC_COMPILATION)

		set(CXX_FLAGS_PEDANTIC_COMPILATION "-Wlogical-op -Wmissing-declarations -Wredundant-decls -Wctor-dtor-privacy -Wdouble-promotion -Wuseless-cast -Wnoexcept")

		if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
			set(CXX_FLAGS_PEDANTIC_COMPILATION "${CXX_FLAGS_PEDANTIC_COMPILATION} -Wdocumentation -Wno-documentation-deprecated-sync")
		endif()

		set(COMPILER_FLAGS "${COMPILER_FLAGS} ${CXX_FLAGS_PEDANTIC_COMPILATION}")

	endif()

# check for sanitizer options
	if(SANITIZE)
		set(COMPILER_FLAGS "${COMPILER_FLAGS} -fsanitize=${SANITIZE}")
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=${SANITIZE}")
		# manually disable some optimizations to get better stacktraces if sanitizers are used
		set(COMPILER_FLAGS "${COMPILER_FLAGS} -fno-omit-frame-pointer -fno-optimize-sibling-calls")
	endif()

### Force colour output (for example for Ninja, or piped CI)

	if(FORCE_COLOR_OUTPUT)
		if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
			set(COMPILER_FLAGS "${COMPILER_FLAGS} -fdiagnostics-color=always")
		elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
			set(COMPILER_FLAGS "${COMPILER_FLAGS} -fcolor-diagnostics")
		endif()
	endif()

### Enable clang-tidy linting
	if (CLANG_TIDY)
		set(CMAKE_CXX_CLANG_TIDY "clang-tidy -Wno-unknown-warning-option")
	endif()

### Set the final compiler flags.

	set(COMPILER_FLAGS "${COMPILER_FLAGS} ${CXX_FLAGS_USER}")

	if(NOT "${CMAKE_CXX_FLAGS}" STREQUAL "${COMPILER_FLAGS}")
		MESSAGE(STATUS "CMake compiler flags set to »${COMPILER_FLAGS}«")
		set(CMAKE_CXX_FLAGS
			"${COMPILER_FLAGS}"
			CACHE
			STRING
			"Global flags used by the CXX compiler during all builds."
			FORCE
		)
	endif()

# #
# Determine optimization level
# #

	if(NOT OPT)
		if(PROFILER STREQUAL "perf")
			set(CMAKE_CXX_FLAGS_RELEASE "-Og")
			set(CMAKE_C_FLAGS_RELEASE "-Og")
		else()
			set(CMAKE_CXX_FLAGS_RELEASE "-O3")
			set(CMAKE_C_FLAGS_RELEASE "-O3")
		endif()
	else()
		set(CMAKE_CXX_FLAGS_RELEASE "${OPT}")
		set(CMAKE_C_FLAGS_RELEASE "${OPT}")

		set(CMAKE_CXX_FLAGS_DEBUG "${OPT}")
		set(CMAKE_C_FLAGS_DEBUG "${OPT}")
	endif()

# check for hardening options
	if(HARDEN)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIE -fstack-protector-strong")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIE -fstack-protector-strong")

		if(APPLE)
			set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fPIE -Wl,-pie")
		elseif(WIN32 AND MINGW)
			set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fPIE -pie")
		else()
			set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fPIE -pie -Wl,-z,relro,-z,now")
		endif()

		if(NOT CMAKE_CXX_FLAGS_DEBUG STREQUAL "-O0")
			add_compile_definitions(_FORTIFY_SOURCE=2)
		endif()
	endif()

	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		set(CMAKE_EXE_LINKER_FLAGS "-lstdc++ -lm ${CMAKE_EXE_LINKER_FLAGS}")
	endif()

	add_compile_definitions(WESNOTH_PATH="${CMAKE_INSTALL_FULL_DATADIR}/${DATADIRNAME}")

	if(X11_FOUND)
		add_compile_definitions(_X11)
	endif()

	add_compile_definitions(LOCALEDIR="${LOCALEDIR}")

# -rdynamic is automatically added, but we don't need it, and it increases the executable size
	RemoveFlag(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS SCRIPT "-rdynamic" "")
	RemoveFlag(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS SCRIPT "-rdynamic" "")

# -DNDEBUG is automatically added to all release build types, so manually remove this define from the related variables
	RemoveFlag(CMAKE_CXX_FLAGS_RELWITHDEBINFO CACHE "-DNDEBUG" "Default C++ flags for RelWithDebInfo")
	RemoveFlag(CMAKE_C_FLAGS_RELWITHDEBINFO CACHE "-DNDEBUG" "Default C flags for RelWithDebInfo")
	RemoveFlag(CMAKE_CXX_FLAGS_MINSIZEREL CACHE "-DNDEBUG" "Default C++ flags for MinSizeRel")
	RemoveFlag(CMAKE_C_FLAGS_MINSIZEREL CACHE "-DNDEBUG" "Default C flags for MinSizeRel")

# #
# Start determining options for Release build
# #

# reset the base Release build option
	MESSAGE("Replacing default flags used for Release build with ${OPT} ${EXTRA_FLAGS_CONFIG} ${EXTRA_FLAGS_RELEASE}")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${EXTRA_FLAGS_CONFIG} ${EXTRA_FLAGS_RELEASE}" CACHE STRING "Release build flags" FORCE)
	set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${EXTRA_FLAGS_CONFIG} ${EXTRA_FLAGS_RELEASE}" CACHE STRING "Release build flags" FORCE)
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "" CACHE STRING "" FORCE)
# set the arch to use for Release build if provided
	if(ARCH)
		MESSAGE("adding -march=${ARCH} to Release build")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=${ARCH}" CACHE STRING "Release build flags" FORCE)
		set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -march=${ARCH}" CACHE STRING "Release build flags" FORCE)
	endif()

# PGO and LTO for GCC
	if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		if(PGO_DATA STREQUAL "generate")
			MESSAGE("Generating PGO data")
			set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fprofile-generate=${CMAKE_SOURCE_DIR}/pgo_data/" CACHE STRING "Release build flags generating PGO data" FORCE)
			set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fprofile-generate=${CMAKE_SOURCE_DIR}/pgo_data/" CACHE STRING "Release build flags generating PGO data" FORCE)
		endif()

		if(PGO_DATA STREQUAL "use")
			MESSAGE("Using PGO data from previous runs")
			set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fprofile-correction -fprofile-use=${CMAKE_SOURCE_DIR}/pgo_data/" CACHE STRING "Release build flags for using PGO data" FORCE)
			set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fprofile-correction -fprofile-use=${CMAKE_SOURCE_DIR}/pgo_data/" CACHE STRING "Release build flags for using PGO data" FORCE)
		endif()

		if(ENABLE_LTO)
			if(NOT LTO_JOBS)
				MESSAGE("LTO_JOBS not set, defaulting to 1")
				set(LTO_JOBS "1" CACHE STRING "Number of threads to use for LTO with gcc" FORCE)
			endif()

			MESSAGE("added -flto=${LTO_JOBS} to Release build")
			set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto=${LTO_JOBS}" CACHE STRING "Release build flags with LTO" FORCE)
			set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -flto=${LTO_JOBS}" CACHE STRING "Release build flags with LTO" FORCE)

			MESSAGE("Using GCC gold linker")
			set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -fuse-ld=gold -Wno-stringop-overflow" CACHE STRING "" FORCE)
		endif()
	endif()

# PGO and LTO for Clang
	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		if(PGO_DATA STREQUAL "generate")
			MESSAGE("Generating PGO data")
			set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fprofile-instr-generate=${CMAKE_SOURCE_DIR}/pgo_data/wesnoth-%p.profraw" CACHE STRING "Release build flags generating PGO data" FORCE)
			set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fprofile-instr-generate=${CMAKE_SOURCE_DIR}/pgo_data/wesnoth-%p.profraw" CACHE STRING "Release build flags generating PGO data" FORCE)
		endif()

		if(PGO_DATA STREQUAL "use")
			MESSAGE("Using PGO data from previous runs")
			set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fprofile-instr-use=${CMAKE_SOURCE_DIR}/pgo_data/wesnoth.profdata" CACHE STRING "Release build flags for using PGO data" FORCE)
			set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fprofile-instr-use=${CMAKE_SOURCE_DIR}/pgo_data/wesnoth.profdata" CACHE STRING "Release build flags for using PGO data" FORCE)
		endif()

		if(ENABLE_LTO)
			MESSAGE("added -flto=thin to Release build")
			set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto=thin" CACHE STRING "Release build flags with LTO" FORCE)
			set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -flto=thin" CACHE STRING "Release build flags with LTO" FORCE)

			MESSAGE("Using Clang LLD linker")
			set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -fuse-ld=lld" CACHE STRING "Linker flag for building with LTO and clang" FORCE)
		endif()
	endif()

# set CMAKE_AR and CMAKE_RANLIB to use LTO-enabled variants if LTO is enabled
	if(ENABLE_LTO)
		MESSAGE("Using gcc-ar and gcc-ranlib")
		find_program(LTO_AR NAMES gcc-ar)
		find_program(LTO_RANLIB NAMES gcc-ranlib)
		set(CMAKE_AR "${LTO_AR}" CACHE STRING "Supports LTO" FORCE)
		set(CMAKE_RANLIB "${LTO_RANLIB}" CACHE STRING "Supports LTO" FORCE)
	endif()
	MARK_AS_ADVANCED(LTO_AR LTO_RANLIB NON_LTO_AR NON_LTO_RANLIB)

# add in extra flags
	set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} ${LINK_EXTRA_FLAGS_CONFIG} ${LINK_EXTRA_FLAGS_RELEASE}")

# clean the pgo data
	set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "${CMAKE_SOURCE_DIR}/pgo_data/")

# #
# End determining options for Release build
# Start setting options for Debug build
# #

	# replace the default Debug flag of -g with -O0 -DDEBUG -ggdb3
	# this matches the flags of scons' debug build
	MESSAGE("Replacing flags used for Debug build ${OPT} -DDEBUG -ggdb3 ${EXTRA_FLAGS_CONFIG} ${EXTRA_FLAGS_DEBUG}")
	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG -ggdb3 ${EXTRA_FLAGS_CONFIG} ${EXTRA_FLAGS_DEBUG}" CACHE STRING "change cmake's Debug flags to match scons' flags" FORCE)
	set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG -ggdb3 ${EXTRA_FLAGS_CONFIG} ${EXTRA_FLAGS_DEBUG}" CACHE STRING "change cmake's Debug flags to match scons' flags" FORCE)
	set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${LINK_EXTRA_FLAGS_CONFIG} ${LINK_EXTRA_FLAGS_DEBUG}" CACHE STRING "" FORCE)

# Enabling GLIBCXX_ASSERTIONS puts bounds-checks on std::vector::operator[], etc
	if(GLIBCXX_ASSERTIONS)
		MESSAGE("Defining _GLIBCXX_ASSERTIONS")
		add_compile_definitions(_GLIBCXX_ASSERTIONS)
	endif()

# GLIBCXX_DEBUG enables more checks that GLIBCXX_ASSERTIONS, but changes the ABI of Boost's program_options library.
# When _GLIBCXX_DEBUG is defined, _GLIBCXX_ASSERTIONS is automatically implied (Gnu's c++config.h will define it).
	if(GLIBCXX_DEBUG)
		MESSAGE("Defining _GLIBCXX_DEBUG and _GLIBCXX_DEBUG_PEDANTIC")
		add_compile_definitions(_GLIBCXX_DEBUG)
		add_compile_definitions(_GLIBCXX_DEBUG_PEDANTIC)
	endif()

# #
# Setup profiler build options
# #
	set(PROFILER "" CACHE STRING "Enable performance-measuring tools (and choose which tool to use)")

	if(PROFILER STREQUAL "gprof")
		MESSAGE("Profiler is gprof")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -pg ${EXTRA_FLAGS_CONFIG}" CACHE STRING "Flags for profiling with gprof" FORCE)
		set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -pg ${EXTRA_FLAGS_CONFIG}" CACHE STRING "Flags for profiling with gprof" FORCE)
	endif()

	if(PROFILER STREQUAL "gcov")
		MESSAGE("Profiler is gcov")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fprofile-arcs -ftest-coverage ${EXTRA_FLAGS_CONFIG}" CACHE STRING "Flags for profiling with gcov" FORCE)
		set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fprofile-arcs -ftest-coverage ${EXTRA_FLAGS_CONFIG}" CACHE STRING "Flags for profiling with gcov" FORCE)
	endif()

	if(PROFILER STREQUAL "gperftools")
		MESSAGE("Profiler is gperftools")
		set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-Wl,--no-as-needed,-lprofiler ${LINK_EXTRA_FLAGS_CONFIG}" CACHE STRING "" FORCE)
	endif()

	if(PROFILER STREQUAL "perf")
		MESSAGE("Profiler is perf")
		set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ggdb -fno-omit-frame-pointer ${EXTRA_FLAGS_CONFIG}" CACHE STRING "Flags for profiling with perf" FORCE)
		set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -ggdb -fno-omit-frame-pointer ${EXTRA_FLAGS_CONFIG}" CACHE STRING "Flags for profiling with perf" FORCE)
	endif()
# #
# End setting profiler build options
# #
else()
	set(CMAKE_CXX_FLAGS "/W3 /WX /wd4351 /wd4250 /wd4244 /wd4267 /we4239 /we4189 /we4100 /wd4275 /EHsc /utf-8 /Zc:__cplusplus" CACHE STRING "Global flags used by the CXX compiler during all builds." FORCE)
	set(CMAKE_C_FLAGS "/WX" CACHE STRING "Global flags used by the C compiler during all builds." FORCE)
	add_compile_definitions(_WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS NOMINMAX _MSVC_STL_HARDENING=1 _MSVC_STL_DESTRUCTOR_TOMBSTONES=1)

	set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG_LUA")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
	set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /NODEFAULTLIB:MSVCRT")

# -DNDEBUG is automatically added to all release build types, so manually remove this define from the related variables
	RemoveFlag(CMAKE_CXX_FLAGS_RELEASE CACHE "/DNDEBUG" "Default C++ flags for RELEASE")
	RemoveFlag(CMAKE_C_FLAGS_RELEASE CACHE "/DNDEBUG" "Default C flags for RELEASE")
	RemoveFlag(CMAKE_CXX_FLAGS_RELWITHDEBINFO CACHE "/DNDEBUG" "Default C++ flags for RelWithDebInfo")
	RemoveFlag(CMAKE_C_FLAGS_RELWITHDEBINFO CACHE "/DNDEBUG" "Default C flags for RelWithDebInfo")
	RemoveFlag(CMAKE_CXX_FLAGS_MINSIZEREL CACHE "/DNDEBUG" "Default C++ flags for MinSizeRel")
	RemoveFlag(CMAKE_C_FLAGS_MINSIZEREL CACHE "/DNDEBUG" "Default C flags for MinSizeRel")
endif()

# When the path starts with a / on a Unix system it's an absolute path.
# This means that on Windows the path used is always relative.
if(IS_ABSOLUTE "${LOCALEDIR}")
	add_compile_definitions(HAS_RELATIVE_LOCALEDIR=0)
	set(LOCALE_INSTALL ${LOCALEDIR})
else()
	add_compile_definitions(HAS_RELATIVE_LOCALEDIR=1)
	set(LOCALE_INSTALL ${CMAKE_INSTALL_DATADIR}/${DATADIRNAME}/${LOCALEDIR})
endif()

add_compile_definitions(FIFODIR="${FIFO_DIR}")

if(PREFERENCES_DIR)
	add_compile_definitions(PREFERENCES_DIR="${PREFERENCES_DIR}")
endif()

if(DEFAULT_PREFS_FILE)
	add_compile_definitions(DEFAULT_PREFS_PATH="${DEFAULT_PREFS_FILE}")

	if(NOT DEFAULT_PREFS_FILE MATCHES "^/")
		add_compile_definitions(HAS_RELATIVE_DEFPREF)
	endif()
endif()

if(ENABLE_DEBUG_WINDOW_LAYOUT)
	add_compile_definitions(DEBUG_WINDOW_LAYOUT_GRAPHS)
endif()

#
# Libraries that are only required by some targets
#

if(ENABLE_GAME OR ENABLE_TESTS)
	find_package(CURL REQUIRED)
	find_package(VorbisFile REQUIRED)
	find_package(PkgConfig REQUIRED)
	find_package(Fontconfig REQUIRED)
	find_package(SDL2 2.0.18 REQUIRED)
	if(NOT MSVC)
		# for everything else, use pkgconfig
		# SDL2_image and SDL2_mixer don't seem to have any cmake configuration available at all
		pkg_check_modules(SDL2IMAGE REQUIRED SDL2_image>=2.0.2)
		pkg_check_modules(SDL2MIXER REQUIRED SDL2_mixer>=2.0.0)
	else()
		# for MSVC, vcpkg builds and provides custom SDL2-related modules for cmake to use, so use those
		# this also fixes the issue with our previous FindSDL2* scripts incorrectly using the Release version of these libs instead of the Debug version
		find_package(SDL2_image CONFIG REQUIRED)
		find_package(SDL2_mixer CONFIG REQUIRED)
	endif()
	pkg_check_modules(CAIRO REQUIRED cairo>=1.10)
	pkg_check_modules(PANGOCAIRO REQUIRED pangocairo>=1.50.0)
	pkg_check_modules(PANGO REQUIRED pango>=1.50.0)
	pkg_check_modules(LIBREADLINE readline)
endif()

if(ENABLE_GAME)
	if(ENABLE_NOTIFICATIONS)
		pkg_check_modules(LIBDBUS dbus-1)
		if(LIBDBUS_FOUND)
			add_compile_definitions(HAVE_LIBDBUS)
		else()
			message("Could not find dbus-1, Disabling notification support.")
		endif()
	else()
		unset(LIBDBUS_FOUND CACHE)
	endif()

	find_package(History)
	if(HISTORY_FOUND)
		add_compile_definitions(HAVE_HISTORY)
	endif()
endif()

if(ENABLE_POT_UPDATE_TARGET)
	find_package(TranslationTools REQUIRED)
endif()

# get languages
if(ENABLE_NLS)
	file(READ po/LINGUAS LINGUAS)
	string(REPLACE "\n" "" LINGUAS ${LINGUAS})
	separate_arguments(LINGUAS)
endif()

#
# Include subdirectories
#

add_subdirectory(doc)

if(GETTEXT_FOUND AND Python_FOUND AND ENABLE_NLS)
	add_subdirectory(po)
endif()

add_subdirectory(src)

#
# Install files
#
if(ENABLE_GAME)
	install(DIRECTORY data fonts images sounds DESTINATION ${CMAKE_INSTALL_DATADIR}/${DATADIRNAME} USE_SOURCE_PERMISSIONS PATTERN ".git" EXCLUDE )
endif()

# install file for add-ons server
if(ENABLE_CAMPAIGN_SERVER AND NOT ENABLE_GAME)
	install(FILES data/COPYING.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/${DATADIRNAME}/data)
endif()

#
# Install desktop file so wesnoth appears in the application start menu with an icon
#
if(ENABLE_DESKTOP_ENTRY AND ENABLE_GAME)
	install(FILES packaging/org.wesnoth.Wesnoth.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications )
	install(DIRECTORY packaging/icons/hicolor packaging/icons/HighContrast DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons )
endif()

if(ENABLE_APPDATA_FILE AND ENABLE_GAME)
	install(FILES packaging/org.wesnoth.Wesnoth.appdata.xml DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo )
endif()

if(ENABLE_SERVER AND FIFO_DIR)
	install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory \$ENV{DESTDIR}/${FIFO_DIR})")
	if(SERVER_UID AND SERVER_GID)
		install(CODE "execute_process(COMMAND chown ${SERVER_UID}:${SERVER_GID} \$ENV{DESTDIR}/${FIFO_DIR})")
	endif()
endif()

#
# uninstall
#

configure_file(
	"${CMAKE_SOURCE_DIR}/cmake/uninstall.cmake.in"
	"${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake"
	IMMEDIATE @ONLY
)

add_custom_target(uninstall
	"${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake"
)

#
# Packaging stuff
#

include(CPack)
set(CPACK_GENERATOR "TGZ")
set(CPACK_SOURCE_GENERATOR "TGZ")
