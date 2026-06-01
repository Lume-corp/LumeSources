local W, H = 640, 480
local TILE = 16
local COLS, ROWS = 40, 30
local pi = 3.14159265
local frame = 0
local GRAV, MAX_FALL = 0.55, 8
local JUMP_VEL, DJUMP_VEL = -9.2, -8
local MOVE_SPD, ACCEL, FRIC, AIR_FRIC = 3.2, 0.8, 0.75, 0.88
local WALL_SLIDE, WALL_JUMP_X, WALL_JUMP_Y = 1.5, 6.5, -8.5
local DASH_SPD, DASH_LEN, DASH_CD = 10, 7, 4
local pl = {
    x=48, y=400, vx=0, vy=0, w=10, h=14, on_ground=false, jumps=2, max_jumps=2,
    can_dash=true, dashing=false, dash_timer=0, dash_cd=0, dash_dx=0, dash_dy=0,
    facing=1, wall_dir=0, dead=false, dead_timer=0, trail={}, squash_x=1, squash_y=1,
    coyote=0, jump_buffer=0
}
local particles, death_particles, dash_sparks = {}, {}, {}
local shake_x, shake_y, shake_amt = 0, 0, 0
local level_tiles = {}
local spikes, coins, springs, checkpoints, moving_plats = {}, {}, {}, {}, {}
local flag = nil
local spawn_x, spawn_y = 48, 400
local coins_collected, total_coins = 0, 0
local cur_room_id = 1
local deaths, time_frames = 0, 0
local keys_prev, keys_now = {}, {}
local function key_just(k) return keys_now[k] and not keys_prev[k] end
local function poll_keys()
    keys_prev = {} for k, v in pairs(keys_now) do keys_prev[k] = v end
    keys_now = {}
    keys_now["left"]  = key_down(VK_LEFT) or key_down(VK_A)
    keys_now["right"] = key_down(VK_RIGHT) or key_down(VK_D)
    keys_now["up"]    = key_down(VK_UP) or key_down(VK_W)
    keys_now["down"]  = key_down(VK_DOWN) or key_down(VK_S)
    keys_now["jump"]  = key_down(VK_SPACE) or key_down(0x5A)
    keys_now["dash"]  = key_down(VK_SHIFT) or key_down(0x58)
end
local function spawn_particles(x, y, count, cr, cg, cb, spd_mult)
    for i = 1, count do
        local a = math.random() * 2 * pi
        local s = (0.5 + math.random() * 2) * (spd_mult or 1)
        table.insert(particles, {x=x, y=y, vx=math.cos(a)*s, vy=math.sin(a)*s-1, life=15+math.random(15), r=cr, g=cg, b=cb, size=1+math.random()*2})
    end
end
local function kill_player()
    if pl.dead then return end
    pl.dead = true; pl.dead_timer = 40; deaths = deaths + 1; shake_amt = 6
    for i = 1, 30 do
        local a, s = math.random() * 2 * pi, 1 + math.random() * 4
        table.insert(death_particles, {x=pl.x+pl.w/2, y=pl.y+pl.h/2, vx=math.cos(a)*s, vy=math.sin(a)*s, life=25+math.random(20), r=0.9, g=0.2, b=0.3, size=2+math.random()*2})
    end
end
local function load_level(id)
    cur_room_id = id
    LEVEL_DATA = nil
    local base_url = get_node_prop("game", "base_url")
    if not base_url or base_url == "" then base_url = "file://celeste/" end
    
    local level_path = base_url .. "levels/level_" .. id .. ".lua"
    local ok, err = load_lua(level_path)
    if not ok or type(LEVEL_DATA) ~= "table" then
        set_text("status", "YOU WIN! Final Time: " .. math.floor(time_frames/30) .. "s")
        flag = nil
        return
    end

    level_tiles = {}
    spikes, coins, springs, checkpoints, moving_plats = {}, {}, {}, {}, {}
    flag = nil
    total_coins, coins_collected = 0, 0

    if LEVEL_DATA.map then
        for ty, row_str in ipairs(LEVEL_DATA.map) do
            level_tiles[ty] = {}
            for tx = 1, #row_str do
                local char = row_str:sub(tx, tx)
                local px, py = (tx-1)*TILE, (ty-1)*TILE
                level_tiles[ty][tx] = (char == '#') and 1 or 0
                if char == 'P' then spawn_x, spawn_y = px, py
                elseif char == 'S' then table.insert(spikes, {x=px, y=py+10, w=TILE, h=6})
                elseif char == 'C' then table.insert(coins, {x=px, y=py, alive=true}); total_coins = total_coins + 1
                elseif char == '^' then table.insert(springs, {x=px, y=py, active=0})
                elseif char == 'F' then flag = {x=px, y=py, active=false, w=TILE, h=TILE}
                end
            end
        end
    end
    if LEVEL_DATA.platforms then
        for _, p in ipairs(LEVEL_DATA.platforms) do
            table.insert(moving_plats, {
                x = p.x, y = p.y, w = p.w, h = p.h,
                start_x = p.x, start_y = p.y,
                move_x = p.move_x, move_y = p.move_y,
                speed = p.speed, progress = 0, dir = 1
            })
        end
    end
    pl.x, pl.y = spawn_x, spawn_y
    pl.vx, pl.vy = 0, 0
    pl.dead, pl.dead_timer = false, 0
    pl.jumps, pl.can_dash = pl.max_jumps, true
    pl.dashing, pl.trail = false, {}
    particles, death_particles, dash_sparks = {}, {}, {}
end
local function rect_overlap(x1,y1,w1,h1, x2,y2,w2,h2)
    return x1 < x2+w2 and x1+w1 > x2 and y1 < y2+h2 and y1+h1 > y2
end
local function solid_rect_static(px, py, pw, ph)
    local left, right = math.floor(px/TILE)+1, math.floor((px+pw-1)/TILE)+1
    local top, bottom = math.floor(py/TILE)+1, math.floor((py+ph-1)/TILE)+1
    for ty = top, bottom do
        for tx = left, right do
            if level_tiles[ty] and level_tiles[ty][tx] == 1 then return true end
        end
    end
    return false
end
local function solid_rect(px, py, pw, ph)
    if solid_rect_static(px, py, pw, ph) then return true end
    for _, p in ipairs(moving_plats) do
        if rect_overlap(px, py, pw, ph, p.x, p.y, p.w, p.h) then return true end
    end
    return false
end
local function update_platforms()
    for _, p in ipairs(moving_plats) do
        p.progress = p.progress + (0.01 * p.speed * p.dir)
        if p.progress >= 1 or p.progress <= 0 then p.dir = -p.dir end
        
        local old_x, old_y = p.x, p.y
        p.x = p.start_x + (p.move_x * p.progress)
        p.y = p.start_y + (p.move_y * p.progress)
        local dx, dy = p.x - old_x, p.y - old_y

        if pl.dead then goto continue end

        local riding = (pl.y + pl.h >= old_y - 1) and (pl.y + pl.h <= old_y + 1) and (pl.x + pl.w > old_x) and (pl.x < old_x + p.w)

        if riding then
            pl.x = pl.x + dx
            pl.y = pl.y + dy
            if solid_rect_static(pl.x, pl.y, pl.w, pl.h) then kill_player() end
        elseif rect_overlap(pl.x, pl.y, pl.w, pl.h, p.x, p.y, p.w, p.h) then
            if dx > 0 then pl.x = p.x + p.w
            elseif dx < 0 then pl.x = p.x - pl.w
            elseif dy > 0 then pl.y = p.y + p.h
            elseif dy < 0 then pl.y = p.y - pl.h
            end
            if solid_rect_static(pl.x, pl.y, pl.w, pl.h) then kill_player() end
        end
        ::continue::
    end
end
local function update_particles_all()
    for i = #particles, 1, -1 do
        local p = particles[i]
        p.x = p.x + p.vx; p.y = p.y + p.vy; p.vy = p.vy + 0.1; p.life = p.life - 1
        if p.life <= 0 then table.remove(particles, i) end
    end
    if #particles > 80 then for i = 1, #particles - 80 do table.remove(particles, 1) end end
    for i = #death_particles, 1, -1 do
        local p = death_particles[i]
        p.x = p.x + p.vx; p.y = p.y + p.vy; p.vy = p.vy + 0.15; p.vx = p.vx * 0.97; p.life = p.life - 1
        if p.life <= 0 then table.remove(death_particles, i) end
    end
    for i = #dash_sparks, 1, -1 do
        local p = dash_sparks[i]
        p.x = p.x + p.vx; p.y = p.y + p.vy; p.life = p.life - 1
        if p.life <= 0 then table.remove(dash_sparks, i) end
    end
    for i = #pl.trail, 1, -1 do
        pl.trail[i].life = pl.trail[i].life - 1
        if pl.trail[i].life <= 0 then table.remove(pl.trail, i) end
    end
end
local function update_player()
    if pl.dead then
        pl.dead_timer = pl.dead_timer - 1
        if pl.dead_timer <= 0 then load_level(cur_room_id) end
        return
    end
    local jump_just, dash_just = key_just("jump"), key_just("dash")
    if pl.dash_cd > 0 then pl.dash_cd = pl.dash_cd - 1 end

    if dash_just and pl.can_dash and not pl.dashing then
        pl.dashing, pl.dash_timer, pl.can_dash, pl.dash_cd = true, DASH_LEN, false, DASH_CD
        local ddx, ddy = 0, 0
        if keys_now["right"] then ddx = 1 elseif keys_now["left"] then ddx = -1 end
        if keys_now["up"] then ddy = -1 elseif keys_now["down"] then ddy = 1 end
        if ddx == 0 and ddy == 0 then ddx = pl.facing end
        local len = math.sqrt(ddx*ddx + ddy*ddy)
        pl.dash_dx, pl.dash_dy = (ddx/len)*DASH_SPD, (ddy/len)*DASH_SPD
        pl.vx, pl.vy, shake_amt = 0, 0, 3
        spawn_particles(pl.x+pl.w/2, pl.y+pl.h/2, 8, 0.3, 0.8, 1.0, 1.5)
    end

    if pl.dashing then
        pl.dash_timer = pl.dash_timer - 1
        pl.vx, pl.vy = pl.dash_dx, pl.dash_dy
        table.insert(pl.trail, {x=pl.x, y=pl.y, life=8, r=0.3, g=0.7, b=1.0})
        if frame % 2 == 0 then
            table.insert(dash_sparks, {
                x=pl.x+pl.w/2-pl.dash_dx*0.5, y=pl.y+pl.h/2-pl.dash_dy*0.5,
                vx=(math.random()-0.5)*2, vy=(math.random()-0.5)*2,
                life=6+math.random(4), r=0.5, g=0.9, b=1.0,
            })
        end
        if pl.dash_timer <= 0 then
            pl.dashing = false
            pl.vx, pl.vy = pl.dash_dx * 0.3, pl.dash_dy * 0.3
            if pl.vy > 0 then pl.vy = 0 end
        end
    else
        local target_vx = 0
        if keys_now["right"] then target_vx = MOVE_SPD; pl.facing = 1 end
        if keys_now["left"] then target_vx = -MOVE_SPD; pl.facing = -1 end
        pl.vx = pl.vx + (target_vx - pl.vx) * (pl.on_ground and ACCEL or 0.3)
        if pl.on_ground and math.abs(target_vx) < 0.1 then pl.vx = pl.vx * FRIC end
        if not pl.on_ground then pl.vx = pl.vx * AIR_FRIC end
        
        pl.vy = pl.vy + GRAV
        if pl.vy > MAX_FALL then pl.vy = MAX_FALL end
        
        if pl.on_ground then pl.coyote = 6 else pl.coyote = pl.coyote - 1 end
        if jump_just then pl.jump_buffer = 6 end
        if pl.jump_buffer > 0 then pl.jump_buffer = pl.jump_buffer - 1 end
        
        if pl.jump_buffer > 0 then
            if pl.coyote > 0 or pl.on_ground then
                pl.vy = JUMP_VEL; pl.jump_buffer = 0; pl.coyote = 0
                pl.jumps = pl.max_jumps - 1
                pl.squash_x, pl.squash_y = 0.75, 1.3
                spawn_particles(pl.x+pl.w/2, pl.y+pl.h, 5, 0.7, 0.7, 0.8, 0.8)
            elseif pl.jumps > 0 then
                pl.vy = DJUMP_VEL; pl.jump_buffer = 0; pl.jumps = pl.jumps - 1
                pl.squash_x, pl.squash_y = 0.7, 1.35
                spawn_particles(pl.x+pl.w/2, pl.y+pl.h, 8, 0.4, 0.7, 1.0, 1.2)
            end
        end
        if not keys_now["jump"] and pl.vy < -2 then pl.vy = pl.vy * 0.65 end
    end

    local sx = (pl.vx > 0) and 1 or -1
    for i = 1, math.ceil(math.abs(pl.vx)) do
        local step = (math.abs(pl.vx) >= 1) and sx or pl.vx
        if not solid_rect(pl.x + step, pl.y, pl.w, pl.h) then pl.x = pl.x + step else pl.vx = 0; break end
    end

    local was_on_ground = pl.on_ground
    pl.on_ground = false
    local syy = (pl.vy > 0) and 1 or -1
    for i = 1, math.ceil(math.abs(pl.vy)) do
        local step = (math.abs(pl.vy) >= 1) and syy or pl.vy
        if not solid_rect(pl.x, pl.y + step, pl.w, pl.h) then
            pl.y = pl.y + step
        else
            if pl.vy > 0 then
                pl.on_ground, pl.jumps, pl.can_dash = true, pl.max_jumps, true
                if not was_on_ground then pl.squash_x, pl.squash_y = 1.3, 0.7; spawn_particles(pl.x+pl.w/2, pl.y+pl.h, 4, 0.7, 0.7, 0.8, 0.5) end
            end
            pl.vy = 0; break
        end
    end

    for _, s in ipairs(spikes) do
        if rect_overlap(pl.x, pl.y, pl.w, pl.h, s.x+2, s.y+2, s.w-4, s.h-2) then kill_player() end
    end

    for _, c in ipairs(coins) do
        if c.alive and rect_overlap(pl.x, pl.y, pl.w, pl.h, c.x, c.y, TILE, TILE) then
            c.alive = false; coins_collected = coins_collected + 1
            spawn_particles(c.x+TILE/2, c.y+TILE/2, 10, 1.0, 0.85, 0.2, 1.5)
            
            -- Активация флага, если собраны все монеты
            if coins_collected == total_coins and flag then
                flag.active = true
                spawn_particles(flag.x+8, flag.y+8, 20, 0.2, 1.0, 0.5, 2.0)
            end
        end
    end

    -- Проверка столкновения с флагом
    if flag and flag.active and rect_overlap(pl.x, pl.y, pl.w, pl.h, flag.x, flag.y, flag.w, flag.h) then
        if LEVEL_DATA.next_level then
            load_level(LEVEL_DATA.next_level)
            return
        end
    end

    if pl.y > H + 20 then kill_player() end
    pl.squash_x = pl.squash_x + (1 - pl.squash_x) * 0.2
    pl.squash_y = pl.squash_y + (1 - pl.squash_y) * 0.2
end
local function render()
    local curW, curH = W, H
    if is_fullscreen() then
        curW, curH = get_window_size()
    end
    if not gl_available() or not gl_begin_render("game", curW, curH) then return end
    gl_viewport(0, 0, curW, curH)
    gl_matrix_mode(GL_PROJECTION) gl_load_identity() 
    gl_ortho(0+shake_x, W+shake_x, H+shake_y, 0+shake_y, -1, 1)
    gl_matrix_mode(GL_MODELVIEW) gl_load_identity()
    gl_begin(GL_QUADS)
    gl_color(0.06, 0.04, 0.12) gl_vertex2f(0,0) gl_vertex2f(W,0)
    gl_color(0.12, 0.08, 0.2) gl_vertex2f(W,H) gl_vertex2f(0,H)
    gl_end()
    gl_point_size(1) gl_begin(GL_POINTS)
    math.randomseed(42)
    for i = 1, 60 do
        local b = 0.15 + math.sin(frame*0.02 + i) * 0.1
        gl_color(b, b, b+0.1)
        gl_vertex2f(math.random()*W, math.random()*H)
    end
    gl_end()
    math.randomseed(math.floor(os.clock()*1000))
    gl_begin(GL_QUADS)
    for ty = 1, ROWS do
        for tx = 1, COLS do
            if level_tiles[ty] and level_tiles[ty][tx] == 1 then
                local x, y = (tx-1)*TILE, (ty-1)*TILE
                local shade = 0.28 + ((tx+ty)%3)*0.03
                gl_color(shade, shade*0.9, shade*1.2)
                gl_vertex2f(x,y) gl_vertex2f(x+TILE,y) gl_vertex2f(x+TILE,y+TILE) gl_vertex2f(x,y+TILE)
                if ty == 1 or level_tiles[ty-1][tx] ~= 1 then
                    gl_color(shade+0.1, shade+0.08, shade+0.15)
                    gl_vertex2f(x,y) gl_vertex2f(x+TILE,y) gl_vertex2f(x+TILE,y+2) gl_vertex2f(x,y+2)
                end
            end
        end
    end
    for _, p in ipairs(moving_plats) do
        gl_color(0.4, 0.5, 0.8)
        gl_vertex2f(p.x, p.y) gl_vertex2f(p.x+p.w, p.y) gl_vertex2f(p.x+p.w, p.y+p.h) gl_vertex2f(p.x, p.y+p.h)
    end
    gl_end()
    gl_begin(GL_TRIANGLES) gl_color(0.9, 0.2, 0.2)
    for _, s in ipairs(spikes) do 
        local count = math.floor(s.w / 6)
        for i = 0, count-1 do
            local bx = s.x + i*6
            local by = s.y + s.h
            gl_vertex2f(bx, by) gl_vertex2f(bx+6, by) gl_vertex2f(bx+3, by-8)
        end
    end
    gl_end()
    if flag then
        gl_begin(GL_QUADS)
        gl_color(0.5, 0.5, 0.5)
        gl_vertex2f(flag.x+2, flag.y) gl_vertex2f(flag.x+4, flag.y)
        gl_vertex2f(flag.x+4, flag.y+16) gl_vertex2f(flag.x+2, flag.y+16)
        if flag.active then gl_color(0.2, 1.0, 0.5) else gl_color(0.3, 0.3, 0.35) end
        local wave = math.sin(frame*0.1) * 2
        gl_vertex2f(flag.x+4, flag.y) gl_vertex2f(flag.x+14+wave, flag.y+2)
        gl_vertex2f(flag.x+12+wave, flag.y+8) gl_vertex2f(flag.x+4, flag.y+10)
        gl_end()
    end
    for _, c in ipairs(coins) do
        if c.alive then
            local cx = c.x+TILE/2; local cy = c.y+TILE/2+math.sin(frame*0.08)*3
            local r = 5+math.sin(frame*0.1)*0.5; local segs = 8
            gl_begin(GL_TRIANGLE_FAN) gl_color(1.0,0.85,0.2) gl_vertex2f(cx,cy)
            gl_color(0.9,0.7,0.1)
            for i = 0, segs do gl_vertex2f(cx+math.cos(i/segs*2*pi)*r, cy+math.sin(i/segs*2*pi)*r) end
            gl_end()
            gl_begin(GL_QUADS) gl_color(1,1,0.8)
            gl_vertex2f(cx-1,cy-2) gl_vertex2f(cx+1,cy-2) gl_vertex2f(cx+1,cy+2) gl_vertex2f(cx-1,cy+2)
            gl_end()
        end
    end
    gl_begin(GL_QUADS)
    for _, t in ipairs(pl.trail) do
        local a = t.life/8
        gl_color(t.r*a, t.g*a, t.b*a)
        gl_vertex2f(t.x,t.y) gl_vertex2f(t.x+pl.w,t.y) gl_vertex2f(t.x+pl.w,t.y+pl.h) gl_vertex2f(t.x,t.y+pl.h)
    end
    gl_end()
    if not pl.dead then
        local cx = pl.x+pl.w/2; local cy = pl.y+pl.h/2
        local hw = (pl.w/2)*pl.squash_x; local hh = (pl.h/2)*pl.squash_y
        local ax = cx-hw; local ay = cy-hh+(pl.h-pl.h*pl.squash_y)*0.5
        local bx = cx+hw; local by = cy+hh+(pl.h-pl.h*pl.squash_y)*0.5
        gl_begin(GL_QUADS)
        if pl.dashing then gl_color(0.3,0.8,1.0)
        elseif not pl.can_dash then gl_color(0.6,0.35,0.4)
        else gl_color(0.85,0.25,0.35) end
        gl_vertex2f(ax,ay) gl_vertex2f(bx,ay) gl_vertex2f(bx,by) gl_vertex2f(ax,by)
        if pl.dashing then gl_color(0.5,0.9,1.0)
        elseif not pl.can_dash then gl_color(0.5,0.3,0.5)
        else gl_color(0.9,0.3,0.45) end
        local hy = ay-4*pl.squash_y
        gl_vertex2f(cx-hw*0.8,hy) gl_vertex2f(cx+hw*0.8,hy) gl_vertex2f(cx+hw*0.6,ay+2) gl_vertex2f(cx-hw*0.6,ay+2)
        gl_end()
        gl_begin(GL_QUADS) gl_color(1,1,1)
        local ex = cx+pl.facing*2
        gl_vertex2f(ex-1.5,ay+5) gl_vertex2f(ex+1.5,ay+5) gl_vertex2f(ex+1.5,ay+8) gl_vertex2f(ex-1.5,ay+8)
        gl_end()
    end
    gl_begin(GL_QUADS)
    for _, p in ipairs(particles) do
        local a = p.life/30
        gl_color(p.r*a, p.g*a, p.b*a)
        gl_vertex2f(p.x-p.size,p.y-p.size) gl_vertex2f(p.x+p.size,p.y-p.size)
        gl_vertex2f(p.x+p.size,p.y+p.size) gl_vertex2f(p.x-p.size,p.y+p.size)
    end
    for _, p in ipairs(death_particles) do
        local a = p.life/40
        gl_color(p.r*a, p.g*a, p.b*a)
        gl_vertex2f(p.x-p.size,p.y-p.size) gl_vertex2f(p.x+p.size,p.y-p.size)
        gl_vertex2f(p.x+p.size,p.y+p.size) gl_vertex2f(p.x-p.size,p.y+p.size)
    end
    for _, p in ipairs(dash_sparks) do
        local a = p.life/10
        gl_color(p.r*a, p.g*a, p.b*a)
        gl_vertex2f(p.x-1,p.y-1) gl_vertex2f(p.x+1,p.y-1) gl_vertex2f(p.x+1,p.y+1) gl_vertex2f(p.x-1,p.y+1)
    end
    gl_end()
    gl_begin(GL_QUADS)
    gl_color(0,0,0,0.5) gl_vertex2f(4,4) gl_vertex2f(130,4) gl_vertex2f(130,22) gl_vertex2f(4,22)
    gl_color(1,0.85,0.2) gl_vertex2f(8,8) gl_vertex2f(16,8) gl_vertex2f(16,18) gl_vertex2f(8,18)
    if pl.can_dash then gl_color(0.3,0.8,1.0) else gl_color(0.3,0.3,0.4) end
    gl_vertex2f(W-30,8) gl_vertex2f(W-8,8) gl_vertex2f(W-8,18) gl_vertex2f(W-30,18)
    gl_end()
    for i = 1, pl.max_jumps do
        gl_begin(GL_QUADS)
        if i <= pl.jumps then gl_color(0.9,0.3,0.4) else gl_color(0.3,0.3,0.4) end
        local jx = W-30-i*14
        gl_vertex2f(jx,8) gl_vertex2f(jx+10,8) gl_vertex2f(jx+10,18) gl_vertex2f(jx,18)
        gl_end()
    end
    if pl.dead then
        gl_begin(GL_QUADS) gl_color(0.2,0,0.05,0.3)
        gl_vertex2f(0,0) gl_vertex2f(W,0) gl_vertex2f(W,H) gl_vertex2f(0,H) gl_end()
    end
    gl_end_render("game")
    gl_refresh("game")
end
local function update()
    frame = frame + 1
    if not pl.dead and (not flag or not flag.active or not rect_overlap(pl.x, pl.y, pl.w, pl.h, flag.x, flag.y, flag.w, flag.h)) then 
        time_frames = time_frames + 1 
    end
    poll_keys()
    update_platforms()
    update_player()
    update_particles_all()
    if shake_amt > 0 then
        shake_x, shake_y = (math.random()-0.5)*shake_amt, (math.random()-0.5)*shake_amt
        shake_amt = shake_amt * 0.8
        if shake_amt < 0.3 then shake_amt, shake_x, shake_y = 0, 0, 0 end
    end
    render()
    if frame % 10 == 0 then
        local secs = math.floor(time_frames/30)
        set_text("status", string.format("Level %s | Coins: %d/%d | Deaths: %d | Time: %02d:%02d", cur_room_id, coins_collected, total_coins, deaths, math.floor(secs/60), secs%60))
    end
end
load_level(1)
set_timer(33, update)