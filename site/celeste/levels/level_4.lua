local TILE = 16
LEVEL_DATA = {
    map = {
        "########################################",
        "#                 #              #     #",
        "# P               #              #     #",
        "#####             #                    #",
        "#                 #                    #",
        "#              C  #                    #",
        "#             #####                    #",
        "#                                      #",
        "#                         ########     #",
        "#                                #     #",
        "#                                #     #",
        "#                                #     #",
        "###########################      #     #",
        "#                                #     #",
        "#                                #     #",
        "#                                #     #",
        "#                                #     #",
        "#                                #     #",
        "# SSSSSSSSSSSSSSSSSSSSSSSSSSSSSS #     #",
        "# ############################## # #####",
        "#                                #     #",
        "#                                ##### #",
        "#                                #     #",
        "#                                # #####",
        "#                                # #####",
        "#                                #     #",
        "#                                ##### #",
        "#                                #     #",
        "#                                #F#####",
        "########################################"
    },
    dash_crystals = {
        {x = 12 * TILE, y = 15 * TILE, timer = 0},
        {x = 5 * TILE, y = 15 * TILE, timer = 0}
    },
    ghost_blocks = {
        {x = 17 * TILE, y = 7 * TILE, w = TILE * 2, h = TILE * 5}
    },
    breakable_blocks = {
        {x = 26 * TILE, y = 9 * TILE, w = TILE, h = TILE * 3, alive = true}
    },
    floor_buttons = {
        {x = 32 * TILE, y = 28 * TILE + 12, w = 16, h = 4, pressed = false, link_id = 1}
    },
    gates = {
        {x = 33 * TILE, y = 3 * TILE, w = TILE, h = TILE * 5, open = false, link_id = 1},
        {x = 32 * TILE, y = 19 * TILE, w = TILE, h = TILE, open = false, link_id = 1}
    },
    rhythm_spikes = {
        {x = 5 * TILE, y = 11 * TILE + 8, w = TILE * 6, h = 8}
    },
    cannons = {
        {x = 38 * TILE, y = 20 * TILE, vx = -6, vy = 0, rate = 90, timer = 0},
        {x = 38 * TILE, y = 25 * TILE, vx = -6, vy = 0, rate = 90, timer = 45}
    },
    next_level = 5
}