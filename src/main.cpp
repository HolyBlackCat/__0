#include "everything.h"

#include <bitset>
#include <iostream>
#include <list>
#include <map>
#include <numeric>
#include <set>
#include <unordered_map>

constexpr ivec2 screen_sz = ivec2(1920,1080)/3;
constexpr int tile_size = 12;

Events::AutoErrorHandlers error_handlers;

Window win("Meow", screen_sz * 2, Window::Settings{}.MinSize(screen_sz).Resizable());
Timing::TickStabilizer tick_stabilizer(60);

Graphics::Texture texture_main(Graphics::Texture::nearest),
                  texture_fbuf_main(Graphics::Texture::nearest, screen_sz), texture_fbuf_scaled(Graphics::Texture::linear);
Graphics::FrameBuffer framebuffer_main = nullptr, framebuffer_scaled = nullptr;

Graphics::Font font_object_main;
Graphics::Font font_object_tiny;
Graphics::CharMap font_main;
Graphics::CharMap font_tiny;

Renderers::Poly2D r;

Input::Mouse mouse;

namespace Shaders
{
    using Graphics::Shader;
    template <typename T> using Uniform   = Graphics::Shader::Uniform<T>;
    template <typename T> using Uniform_v = Graphics::Shader::Uniform_v<T>;
    template <typename T> using Uniform_f = Graphics::Shader::Uniform_f<T>;
    using Graphics::Texture;
    namespace Identity
    {
        ReflectStruct(Att, (
            (fvec2)(pos),
        ))
        ReflectStruct(Uni, (
            (Uniform_f<Texture>)(texture),
        ))
        Uni uni;
        Shader object = Shader::Make<Att>("Identity", R"(
VARYING(vec2, pos)
void main()
{
    v_pos = a_pos;
    gl_Position = vec4(a_pos * 2. - 1., 0, 1);
})", R"(
VARYING(vec2, pos)
void main()
{
    gl_FragColor = texture2D(u_texture, v_pos);
})", &uni);
    }
}

namespace Draw
{
    inline namespace TextPresets
    {
        void WithBlackOutline(Renderers::Poly2D::Text_t &obj) // Is a preset
        {
            obj.callback([](Renderers::Poly2D::Text_t::CallbackParams params)
            {
                if (params.render_pass && params.render.size())
                {
                    constexpr ivec2 offset_list[]{{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
                    auto copy = params.render[0];
                    fvec3 saved_color = copy.color;
                    float saved_alpha = copy.alpha;
                    copy.color = fvec3(0);
                    copy.alpha *= 0.6;
                    params.render.clear();
                    for (ivec2 offset : offset_list)
                    {
                        auto tmp = copy;
                        tmp.matrix.z.x += offset.x;
                        tmp.matrix.z.y += offset.y;
                        params.render.push_back(tmp);
                    }
                    copy.color = saved_color;
                    copy.alpha = saved_alpha;
                    params.render.push_back(copy);
                }
            });
        }

        /* Colors:
         * /r - grey
         * /1 - white
         * /2 - green
         * /3 - red
         * /4 - gold
         * /5 - blue
         */
        [[nodiscard]] auto WithColors(const std::vector<fvec3> &colors = {fvec3(1), fvec3(0.25,1,0.5), fvec3(1,0.25,0.25), fvec3(1,5/6.,1/6.), fvec3(0,0.5,1)}) // Returns a preset
        {
            if (colors.size() > 7)
                Program::Error("Too many text colors.");
            return [=](Renderers::Poly2D::Text_t &obj)
            {
                obj.color(fvec3(0.6)).callback([=, color_index = 0](Renderers::Poly2D::Text_t::CallbackParams params) mutable
                {
                    if (params.ch == '\r' || (params.ch >= '\1' && params.ch <= '\7'))
                    {
                        if (params.render.size())
                            params.render.clear();
                        params.glyph.advance = 0;

                        if (params.ch == '\r')
                        {
                            color_index = 0;
                        }
                        else
                        {
                            color_index = params.ch - '\0';
                            if (color_index > int(colors.size()))
                                Program::Error("Text color index is out of range.");
                        }
                    }

                    if (params.render_pass && color_index != 0)
                    {
                        for (auto &it : params.render)
                            it.color = colors[color_index-1];
                    }
                });
            };
        }

        [[nodiscard]] auto WithCursor(int index, fvec3 color = fvec3(tick_stabilizer.ticks % 60 < 30)) // Returns a preset
        {
            return [=](Renderers::Poly2D::Text_t &obj)
            {
                obj.callback([=](Renderers::Poly2D::Text_t::CallbackParams params)
                {
                    constexpr int width = 1;
                    if (params.render_pass && params.index == index)
                    {
                        r.Quad(params.pos - ivec2(width, params.obj.state().ch_map->Ascent()), ivec2(1, params.obj.state().ch_map->Height()))
                         .color(color).alpha(params.render[0].alpha).beta(params.render[0].beta);
                    }
                });
            };
        }
    }

    ivec2 scaled_size;

    void Resize()
    {
        float scale = (win.Size() / fvec2(screen_sz)).min();
        Draw::scaled_size = iround(screen_sz * scale) * 2 / 2;
        float scale_fl = floor(scale);
        ivec2 scaled_size_fl = iround(screen_sz * scale_fl) * 2 / 2;
        texture_fbuf_scaled.SetData(scaled_size_fl);
        mouse.Transform(win.Size() / 2, screen_sz.x / float(Draw::scaled_size.x));
    }

    void ReloadTextures()
    {
        Graphics::Image textureimage_main("assets/texture.png");
        Graphics::Font::MakeAtlas(textureimage_main, ivec2(0,256), ivec2(256,256),
        {
            {font_object_main, font_main, Graphics::Font::light, Strings::Encodings::cp1251()},
            {font_object_tiny, font_tiny, Graphics::Font::light, Strings::Encodings::cp1251()},
        });
        /*
        font_main.EnableLineGap(0);
        font_tiny.EnableLineGap(0);
        */

        texture_main.SetData(textureimage_main);
    }

    void Init()
    {
        static bool once = 1;
        if (once) once = 0;
        else Program::Error("Draw::Init() was called twice.");

        Graphics::ClearColor(fvec3(0));
        Graphics::Blending::Enable();
        Graphics::Blending::FuncNormalPre();

        font_object_main.Create("assets/CatIV15.ttf", 15);
        font_object_tiny.Create("assets/CatTiny11.ttf", 11);

        ReloadTextures();

        r.Create(0x10000);
        r.SetTexture(texture_main);
        r.SetMatrix(fmat4::ortho2D(screen_sz / ivec2(-2,2), screen_sz / ivec2(2,-2)));
        r.SetDefaultFont(font_main);

        Resize();

        framebuffer_main  .Attach(texture_fbuf_main);
        framebuffer_scaled.Attach(texture_fbuf_scaled);
    }

    void FullscreenQuad()
    {
        using Shaders::Identity::Att;
        Att array[] {{{-2,-2}},{{-2,10}},{{10,-2}}};
        static Graphics::VertexBuffer<Att> buf(std::extent_v<decltype(array)>, array);
        buf.Draw(Graphics::triangles);
    }

    void Rect(ivec2 pos, ivec2 size, int width, fvec3 color, float alpha = 1, float beta = 1)
    {
        ivec2 a = pos, b = pos + size;
        r.Quad(ivec2(a.x-width,a.y), ivec2(b.x,a.y-width)).absolute().color(color).alpha(alpha).beta(beta);
        r.Quad(ivec2(a.x,a.y), ivec2(a.x-width,b.y+width)).absolute().color(color).alpha(alpha).beta(beta);
        r.Quad(ivec2(a.x,b.y), ivec2(b.x+width,b.y+width)).absolute().color(color).alpha(alpha).beta(beta);
        r.Quad(ivec2(b.x,a.y-width), ivec2(b.x+width,b.y)).absolute().color(color).alpha(alpha).beta(beta);
    }
}

namespace Objects
{
    class Camera
    {
      public:
        ivec2 pos;

        Camera(ivec2 pos) : pos(pos) {}
    };

    class Background
    {
      public:
        void Tick()
        {

        }
        void Render()
        {
            constexpr fvec3 c1 = fvec3(0.1),
                            c2 = fvec3(0.2);
            r.Quad(-screen_sz/2, screen_sz).color(c1,c1,c2,c2);
        }
    };


    class Map
    {
      public:
        static constexpr uint32_t version_magic = 5; // This should be changed when map binary structure changes.

        static constexpr ivec2 sheet_size = ivec2(32), sheet_tex_pos = ivec2(0,512);


        enum SafetyMode {Safe, Unsafe};


        using tile_id_t = int;
        inline static constexpr tile_id_t no_tile = tile_id_t(-1);

        ReflectStruct(Tile, (
            (tile_id_t)(front,mid,back)(=no_tile),
        ))


        using layer_mem_ptr_t = tile_id_t Tile::*;

        static constexpr layer_mem_ptr_t front = &Tile::front,
                                         mid   = &Tile::mid,
                                         back  = &Tile::back;

        static constexpr layer_mem_ptr_t layer_list[] {front, mid, back};
        static constexpr int layer_count = std::extent_v<decltype(layer_list)>;


        ReflectMemberEnum(LayerEnum, (la_front)(la_mid)(la_back)(num_layers))

        static_assert(layer_list[la_front] == front &&
                      layer_list[la_mid  ] == mid   &&
                      layer_list[la_back ] == back    );

        class Tiling
        {
          public:
            struct Group
            {
                Reflect(Group)
                (
                    (std::string)(name),
                    (std::vector<std::string>)(tiles),
                    (std::vector<int> indices = {};),
                )

                void Finalize()
                {
                    // Check for empty name
                    if (name.empty())
                        throw std::runtime_error("Attempt to create a tile group with an empty name.");

                    // Sort
                    std::sort(tiles.begin(), tiles.end());

                    // Check for duplicates
                    if (auto it = std::adjacent_find(tiles.begin(), tiles.end()); it != tiles.end())
                        throw std::runtime_error(Str("Duplicate tile named `", *it, "` in group `", name, "`."));
                }

                bool Contains(std::string name) const
                {
                    return std::binary_search(tiles.begin(), tiles.end(), name);
                }
                bool Contains(int index) const
                {
                    return std::binary_search(indices.begin(), indices.end(), index);
                }

                explicit operator const std::string &() const {return name;}
                template <typename A, typename B, Group * = nullptr> friend bool operator< (const A &a, const B &b) {return static_cast<const std::string &>(a) <  static_cast<const std::string &>(b);}
                template <typename A, typename B, Group * = nullptr> friend bool operator==(const A &a, const B &b) {return static_cast<const std::string &>(a) == static_cast<const std::string &>(b);}
                // Dummy template parameter stops compiler from complaining about redefinitions of these functions in other structures.
            };

            struct TileVariant
            {
                Reflect(TileVariant)
                (
                    (std::string)(name),
                    (ivec2)(texture),
                    (ivec2)(size)(=ivec2(1)),
                    (ivec2)(offset)(=ivec2(0)),
                    (ivec2)(tex_offset)(=ivec2(0)),
                )

                int global_index;

              private:
                bool small;
                ivec2 effective_texture_pixel_pos,
                      effective_texture_pixel_size,
                      effective_texture_pixel_offset;
              public:

                void Finalize(std::string tile_name) // This is not const to prevent calling it from outside.
                {
                    if (name.empty())
                        throw std::runtime_error(Str("Variant `", name, "` of tile `", tile_name, "` has empty name."));

                    if ((size < 1).any())
                        throw std::runtime_error(Str("Variant `", name, "` of tile `", tile_name, "` has non-positive size."));
                    if ((texture < 0).any() || (texture + size > sheet_size).any())
                        throw std::runtime_error(Str("Texture coordinates for variant `", name, "` of tile `", tile_name, "` are out of range."));

                    small = (size == ivec2(1));
                    effective_texture_pixel_pos    = sheet_tex_pos + (texture + offset + tex_offset) * tile_size;
                    effective_texture_pixel_size   = size * tile_size;
                    effective_texture_pixel_offset = offset * tile_size;
                }

                ivec2 TexturePos() const
                {
                    return effective_texture_pixel_pos;
                }
                ivec2 TextureSize() const
                {
                    return effective_texture_pixel_size;
                }
                ivec2 TextureOffset() const
                {
                    return effective_texture_pixel_offset;
                }
                bool Small() const
                {
                    return small;
                }

                explicit operator const std::string &() const {return name;}
                template <typename A, typename B, TileVariant * = nullptr> friend bool operator< (const A &a, const B &b) {return static_cast<const std::string &>(a) <  static_cast<const std::string &>(b);}
                template <typename A, typename B, TileVariant * = nullptr> friend bool operator==(const A &a, const B &b) {return static_cast<const std::string &>(a) == static_cast<const std::string &>(b);}
                // Dummy template parameter stops compiler from complaining about redefinitions of these functions in other structures.
            };

            struct TileRule
            {
                ReflectStruct(Result, (
                    (std::string)(name),
                    (float)(chance)(=-1),
                    (int index;), // Sic! We don't reflect this.
                ))
                ReflectStruct(Requirement, (
                    (std::string)(name), // This can be `""`, which means 'any tile` (then `index == -1`, `is_group == 0`).
                    (ivec2)(offset),
                    (int index;), // Tile index or group index.
                    (bool is_group;),
                ))
                ReflectStruct(ModuloPosition, (
                    (ivec2)(size),
                    (std::set<ivec2>)(offsets),
                    (bool apply = 0;),
                ))

                ReflectStruct(Dupe, (
                    (imat2)(matrix), // This matrix will applied to each offset of the new rule.
                    (std::vector<Result>)(results)(={}), // If this is not empty, it will be used instead of the original rule list.
                ))

                Reflect(TileRule)
                (
                    (int original_index;),
                    (std::vector<Result>)(results),
                    (std::vector<Requirement>)(requires,requires_not)(={}),
                    (std::vector<std::string>)(req_variants)(={}), // The current tile must have one of those variants for the rule to work.
                    (std::vector<imat2>)(matrices)(={}), // All requirements will be copied with these matrices applied to offsets (after handling `duplicate`).
                    (ModuloPosition)(modulo_pos)(={ivec2(1), {}}), // The current tile position modulo `size` must be one of `offsets` for this rule to work.
                    (std::vector<Dupe>)(duplicate)(={}), // This rule will be duplicated for each element of this vector.
                    (std::vector<int> req_variant_indices = {};), // This will be sorted.
                )

                bool CanBeAppliedToVariant(int variant_index) const
                {
                    if (req_variant_indices.empty())
                        return 1;
                    else
                        return std::binary_search(req_variant_indices.begin(), req_variant_indices.end(), variant_index);
                }

                void Finalize(std::string tile_name)
                {
                    { // Check that result vector is not empty
                        if (results.empty())
                            throw std::runtime_error(Str("Result vector of rule ", original_index, " for tile `", tile_name, "` is empty."));
                    }

                    { // Fix chances
                        float sum = 0;
                        int need_init = 0;
                        for (const auto &it : results)
                        {
                            if (it.chance >= 0)
                                sum += it.chance;
                            else
                                need_init++;
                        }
                        if (sum > 1)
                            throw std::runtime_error(Str("Results of the rule ", original_index, " for tile `", tile_name, "` have total probability greater than 1."));
                        if (need_init > 0)
                        {
                            sum = (1 - sum) / need_init;
                            for (auto &it : results)
                                if (it.chance < 0)
                                    it.chance = sum;
                        }
                    }

                    { // Check modulo position settings
                        if ((modulo_pos.size < 1).any())
                            throw std::runtime_error(Str("Rectangle size for modulo position for the rule ", original_index, " for tile `", tile_name, "` is smaller than 1 in at least one dimension."));

                        modulo_pos.apply = (modulo_pos.size != ivec2(1));

                        if (modulo_pos.apply && modulo_pos.offsets.empty())
                            throw std::runtime_error(Str("List of modulo offsets for the rule ", original_index, " for tile `", tile_name, "` is empty."));

                        for (const auto &offset : modulo_pos.offsets)
                            if ((offset < 0).any() || (offset >= modulo_pos.size).any())
                                throw std::runtime_error(Str("Modulo offset ", offset," for the rule ", original_index, " for tile `", tile_name, "` is invalid."));
                    }

                    { // Copy requirements according to matrices
                        for (auto mem_ptr : {&TileRule::requires, &TileRule::requires_not})
                        {
                            auto &vec = this->*mem_ptr;
                            auto copy = vec;

                            for (const auto &matrix : matrices)
                            {
                                auto tmp = copy; // Sic! We can't operate directly on the copy.
                                for (auto &req : tmp)
                                    req.offset = matrix /mul/ req.offset;
                                vec.insert(vec.end(), tmp.begin(), tmp.end());
                            }
                        }
                    }
                }
            };

            struct Tile
            {
                Reflect(Tile)
                (
                    (std::string)(name),
                    (std::set<std::string>)(flags)(={}),
                    (LayerEnum)(layer),
                    (std::string)(va_default,va_display),
                    (std::vector<TileVariant>)(variants),
                    (std::vector<TileRule>)(rules),
                )

                std::vector<char> flag_array; // `char` is used instead of `bool` because I don't want packing.

                int original_index;
                int va_default_index, va_display_index;


                bool HasFlag(int flag_index) const
                {
                    return flag_array[flag_index];
                }

                int VariantIndex(std::string variant_name) // This is not const to prevent calling it from outside.
                {
                    auto it = std::lower_bound(variants.begin(), variants.end(), variant_name);
                    if (it == variants.end() || it->name != variant_name)
                        return -1;
                    return it - variants.begin();
                }

                const TileVariant &Variant(std::string variant_name) const
                {
                    auto it = std::lower_bound(variants.begin(), variants.end(), variant_name);
                    if (it == variants.end() || it->name != variant_name)
                        throw std::runtime_error(Str("Tile `", name, "` has no variant `", variant_name, "`."));
                    return *it;
                }

                const TileVariant &DefaultVariant() const
                {
                    return variants[va_default_index];
                }
                const TileVariant &DisplayVariant() const
                {
                    return variants[va_display_index];
                }

                void Finalize(int index)
                {
                    // Assign index
                    original_index = index;

                    // Check for empty name
                    if (name.empty())
                        throw std::runtime_error("Attempt to create a tile with an empty name.");

                    // Validate layer enum
                    if (layer < 0 || layer >= num_layers)
                        throw std::runtime_error(Str("Invalid layer enum value for tile `", name, "`."));

                    { // Variants
                        // Validate textures
                        for (auto &it : variants)
                            it.Finalize(name);
                        // Sort
                        std::sort(variants.begin(), variants.end());
                        // Check for duplicates
                        if (auto it = std::adjacent_find(variants.begin(), variants.end()); it != variants.end())
                            throw std::runtime_error(Str("Duplicate variant `", it->name, "` for tile `", name, "`."));
                        // Get indices for default/display variants
                        va_default_index = VariantIndex(va_default);
                        if (va_default_index == -1)
                            throw std::runtime_error(Str("Default variant `", va_default, "` for tile `", name, "` doesn't exist."));
                        va_display_index = VariantIndex(va_display);
                        if (va_display_index == -1)
                            throw std::runtime_error(Str("Display variant `", va_display, "` for tile `", name, "` doesn't exist."));
                    }

                    { // Rules
                        { // Assign indices
                            int index = 0;
                            for (auto &it : rules)
                                it.original_index = index++;
                        }

                        // Make duplicates (we do it before finalizing, because finalizing applies requirement matrices and checks result vectors)
                        for (auto rule_it = rules.begin(); rule_it != rules.end();)
                        {
                            auto &original_rule = *rule_it++; // Sic! We want to increment it now to insert at the right place later.

                            for (const auto &dupe : original_rule.duplicate)
                            {
                                rule_it = rules.insert(rule_it, original_rule);

                                for (auto mem_ptr : {&TileRule::requires, &TileRule::requires_not})
                                for (auto &req : (*rule_it).*mem_ptr)
                                    req.offset = dupe.matrix /mul/ req.offset;

                                if (dupe.results.size() > 0)
                                    rule_it->results = dupe.results;

                                rule_it++;
                            }
                        }

                        // Finalize
                        for (auto &it : rules)
                            it.Finalize(name);

                        // Get variant indices for results and required variants
                        for (auto &rule : rules)
                        {
                            for (auto &result : rule.results)
                            {
                                result.index = VariantIndex(result.name);
                                if (result.index == -1)
                                    throw std::runtime_error(Str("A tiling rule result for tile `", name, "` references non-existent variant named `", result.name, "`."));
                            }

                            for (const auto &va_name : rule.req_variants)
                            {
                                int index = VariantIndex(va_name);
                                if (index == -1)
                                    throw std::runtime_error(Str("A tiling rule for tile `", name, "` references non-existent variant named `", va_name, "`."));
                                rule.req_variant_indices.push_back(index);
                            }
                            std::sort(rule.req_variant_indices.begin(), rule.req_variant_indices.end());
                        }
                    }
                }

                explicit operator const std::string &() const {return name;}
                template <typename A, typename B, Tile * = nullptr> friend bool operator< (const A &a, const B &b) {return static_cast<const std::string &>(a) <  static_cast<const std::string &>(b);}
                template <typename A, typename B, Tile * = nullptr> friend bool operator==(const A &a, const B &b) {return static_cast<const std::string &>(a) == static_cast<const std::string &>(b);}
                // Dummy template parameter stops compiler from complaining about redefinitions of these functions in other structures.
            };

            struct Data
            {
                Reflect(Data)
                (
                    (std::vector<std::string>)(flags),
                    (std::vector<Group>)(groups),
                    (std::vector<Tile>)(tiles),
                )

                std::vector<int> layer_tile_indices[num_layers];

                struct TileInfo
                {
                    int tile_index;
                    int variant_index;
                    std::string tile_name;
                    std::string variant_name;
                };
                std::map<tile_id_t, TileInfo> tile_info;

                std::map<std::string, std::map<std::string, tile_id_t>> indices_by_name;
                tile_id_t global_index_count;

                ivec2 autotiling_range;


                ivec2 max_texture_offset_negative = ivec2(std::numeric_limits<int>::max()),
                      max_texture_offset_positive = ivec2(std::numeric_limits<int>::min());


                bool GroupExists(std::string name) const
                {
                    return std::binary_search(groups.begin(), groups.end(), name);
                }
                bool TileExists(std::string name) const
                {
                    return std::binary_search(tiles.begin(), tiles.end(), name);
                }

                // Those return -1 if there is no such object.
                int TileFlagIndex(std::string name) const
                {
                    auto it = std::lower_bound(flags.begin(), flags.end(), name);
                    if (it == flags.end() || *it != name)
                        return -1;
                    return it - flags.begin();
                }
                int GroupIndex(std::string name) const
                {
                    auto it = std::lower_bound(groups.begin(), groups.end(), name);
                    if (it == groups.end() || it->name != name)
                        return -1;
                    return it - groups.begin();
                }
                int TileIndex(std::string name) const
                {
                    auto it = std::lower_bound(tiles.begin(), tiles.end(), name);
                    if (it == tiles.end() || it->name != name)
                        return -1;
                    return it - tiles.begin();
                }

                void Finalize()
                {
                    { // Tile flags
                        // Sort
                        std::sort(flags.begin(), flags.end());
                        // Check for duplicates
                        if (auto it = std::adjacent_find(flags.begin(), flags.end()); it != flags.end())
                            throw std::runtime_error(Str("A duplicate tile flag name `", *it, "`."));
                    }

                    { // Groups
                        // Finalize
                        for (auto &it : groups)
                            it.Finalize();
                        // Sort
                        std::sort(groups.begin(), groups.end());
                        // Check for duplicates
                        if (auto it = std::adjacent_find(groups.begin(), groups.end()); it != groups.end())
                            throw std::runtime_error(Str("A duplicate tile group named `", it->name, "`."));
                    }

                    { // Tiles
                        // Finalize
                        int index = 0;
                        for (auto &it : tiles)
                            it.Finalize(index++);
                        // Sort
                        std::sort(tiles.begin(), tiles.end());
                        // Check for duplicates
                        if (auto it = std::adjacent_find(tiles.begin(), tiles.end()); it != tiles.end())
                            throw std::runtime_error(Str("A duplicate tile named `", it->name, "`."));
                        // Check for collision with group names
                        for (const auto &it : tiles)
                            if (GroupExists(it.name))
                                throw std::runtime_error(Str("A name collision between a tile named `", it.name, "` and a group with the same name."));

                        // Handle flags
                        for (auto &it : tiles)
                        {
                            it.flag_array.resize(flags.size()); // This fills the vector with zeroes.

                            for (const auto &flag : it.flags)
                            {
                                int flag_index = TileFlagIndex(flag);
                                if (flag_index == 1)
                                    throw std::runtime_error(Str("An invalid flag named `", flag, "` was specified for tile `", it.name, "`."));
                                it.flag_array[flag_index] = 1;
                            }
                        }
                    }

                    { // Get tile indices for groups
                        for (auto &group : groups)
                        {
                            for (const auto &name : group.tiles)
                            {
                                int index = TileIndex(name);
                                if (index == -1)
                                    throw std::runtime_error(Str("Tile named `", name, "` referenced in group `", group.name, "` doesn't exist."));
                                group.indices.push_back(index);
                            }

                            std::sort(group.indices.begin(), group.indices.end());
                        }
                    }

                    { // Generate tile IDs
                        tile_id_t index = 0;

                        for (std::size_t tile_index = 0; tile_index < tiles.size(); tile_index++)
                        {
                            auto &tile = tiles[tile_index];

                            for (std::size_t variant_index = 0; variant_index < tile.variants.size(); variant_index++)
                            {
                                auto &variant = tile.variants[variant_index];
                                variant.global_index = index++;
                                indices_by_name[tile.name][variant.name] = variant.global_index;

                                TileInfo info;
                                info.tile_index = tile_index;
                                info.variant_index = variant_index;
                                info.tile_name = tile.name;
                                info.variant_name = variant.name;
                                tile_info.insert(std::make_pair(variant.global_index, info));
                            }
                        }

                        global_index_count = index;
                    }

                    { // Get max texture offsets
                        for (const auto &tile : tiles)
                        {
                            for (const auto &variant : tile.variants)
                            {
                                ivec2 a = variant.offset, b = variant.offset + variant.size - 1;
                                if (a.x < max_texture_offset_negative.x) max_texture_offset_negative.x = a.x;
                                if (a.y < max_texture_offset_negative.y) max_texture_offset_negative.y = a.y;
                                if (b.x > max_texture_offset_positive.x) max_texture_offset_positive.x = b.x;
                                if (b.y > max_texture_offset_positive.y) max_texture_offset_positive.y = b.y;
                            }
                        }
                    }

                    { // Obtain tile lists for specific layers
                        for (int i = 0; i < num_layers; i++)
                        {
                            auto &vec = layer_tile_indices[i];
                            int tile_index = 0;
                            for (const auto &it : tiles)
                            {
                                if (it.layer == i)
                                    vec.push_back(tile_index);
                                tile_index++;
                            }
                            // Sort them back into the order they appeared in the file.
                            std::sort(vec.begin(), vec.end(), [&](int a, int b){return tiles[a].original_index < tiles[b].original_index;});
                        }
                    }

                    { // Get max autotiling range
                        autotiling_range = ivec2(0);
                        for (const auto &tile : tiles)
                        for (const auto &rule : tile.rules)
                        for (const auto *req_list : {&rule.requires, &rule.requires_not})
                        for (const auto &req : *req_list)
                        {
                            ivec2 range = abs(req.offset);
                            if (autotiling_range.x < range.x) autotiling_range.x = range.x;
                            if (autotiling_range.y < range.y) autotiling_range.y = range.y;
                        }
                    }

                    { // Get indices for autotiling requirements
                        for (auto &tile : tiles)
                        for (auto &rule : tile.rules)
                        for (auto *req_list : {&rule.requires, &rule.requires_not})
                        for (auto &req : *req_list)
                        {
                            req.index = TileIndex(req.name);
                            req.is_group = 0;

                            if (req.index == -1)
                            {
                                if (req.name == "")
                                    continue;

                                req.index = GroupIndex(req.name);
                                req.is_group = 1;

                                if (req.index == -1)
                                    throw std::runtime_error(Str("Autotiling rule for tile `", tile.name, "` references `", req.name, "`, which is neither a tile nor a tile group."));
                            }
                        }
                    }
                }
            };

          private:
            Data data;

            std::string file_name;

          public:
            Tiling(std::string file_name) : file_name(file_name)
            {
                Reload(1);
            }

            void Reload(bool fatal_errors = 0)
            {
                Data data_copy = data;
                data = {};

                Utils::MemoryFile file(file_name);

                try
                {
                    std::string error_message;
                    if (auto ptr = Reflection::from_string(data, (char *)file.Data(), &error_message); ptr != (char *)file.Data() + file.Size())
                        throw std::runtime_error(Str("Unable to parse tiling settings:\n", (ptr == 0 ? error_message : "Extra data at the end of input.")));

                    data.Finalize();
                }
                catch (std::runtime_error &e)
                {
                    if (fatal_errors)
                        Program::Error(e.what());

                    UI::MessageBox("Error!", e.what(), UI::warning);
                    data = data_copy;
                }
            }

            int FlagIndex(std::string name) const // This fails with a error if such flag doesn't exist.
            {
                auto it = std::lower_bound(data.flags.begin(), data.flags.end(), name);
                if (it == data.flags.end() || *it != name)
                    Program::Error(Str("Attempt to access non-existent tile flag `", name, "`."));
                return it - data.flags.begin();
            }

            int IndexCount() const // Valid tile_id_t values are: 0 <= x < IndexCount().
            {
                return data.global_index_count;
            }
            tile_id_t IndexByName(std::string tile_name, std::string variant_name) const // Returns -1 if no such tile or variant.
            {
                auto tile_it = data.indices_by_name.find(tile_name);
                if (tile_it == data.indices_by_name.end())
                    return -1;
                auto variant_it = tile_it->second.find(variant_name);
                if (variant_it == tile_it->second.end())
                    return -1;
                return variant_it->second;
            }
            int GetTileIndex(tile_id_t id) const
            {
                auto it = data.tile_info.find(id);
                if (it == data.tile_info.end())
                    Program::Error(Str("Attempt to get tile index for id ", ivec2(id), " which doesn't exist."));
                return it->second.tile_index;
            }
            int GetVariantIndex(tile_id_t id) const
            {
                auto it = data.tile_info.find(id);
                if (it == data.tile_info.end())
                    Program::Error(Str("Attempt to get tile variant index for id ", ivec2(id), " which doesn't exist."));
                return it->second.variant_index;
            }

            const Tile &GetTile(tile_id_t id) const
            {
                auto it = data.tile_info.find(id);
                if (it == data.tile_info.end())
                    Program::Error(Str("Attempt to get tile for id ", ivec2(id), " which doesn't exist."));
                return data.tiles[it->second.tile_index];
            }
            const TileVariant &GetVariant(tile_id_t id) const
            {
                auto it = data.tile_info.find(id);
                if (it == data.tile_info.end())
                    Program::Error(Str("Attempt to get tile variant for id ", ivec2(id), " which doesn't exist."));
                return data.tiles[it->second.tile_index].variants[it->second.variant_index];
            }

            const std::vector<TileRule> &GetTileRules(int tile_index) const
            {
                return data.tiles[tile_index].rules;
            }

            // To make sure all the large tile textures get into your camera, increase rendered tile range by those values (add them to top-left and bottom-right corners respectively).
            ivec2 MaxTextureOffsetNegative() const
            {
                return data.max_texture_offset_negative;
            }
            ivec2 MaxTextureOffsetPositive() const
            {
                return data.max_texture_offset_positive;
            }

            int TileCount() const
            {
                return data.tiles.size();
            }

            const Tile &TileByIndex(int index) const
            {
                return data.tiles[index];
            }
            const Group &GroupByIndex(int index) const
            {
                return data.groups[index];
            }

            const TileVariant &DefaultVariant(int tile_index) const
            {
                return Map::tiling.TileByIndex(tile_index).DefaultVariant();
            }

            const std::vector<int> &TileIndicesForLayer(LayerEnum layer) const
            {
                return data.layer_tile_indices[layer];
            }

            ivec2 AutotilingRange() const
            {
                return data.autotiling_range;
            }
        };
        inline static Tiling tiling{"assets/tiling"};
        inline static int flag_solid = tiling.FlagIndex("solid");

      private:
        std::string file_name;

        struct Data
        {
            Reflect(Data)
            (
                (ivec2)(size),
                (std::vector<Tile>)(tiles),
            )

            template <SafetyMode Mode = Safe> void Set(ivec2 pos, const Tile &tile)
            {
                if constexpr (Mode != Unsafe)
                    if ((pos < 0).any() || (pos >= size).any())
                        return;
                tiles[pos.x + size.x * pos.y] = tile;
            }
            template <SafetyMode Mode = Safe> void Set(ivec2 pos, layer_mem_ptr_t layer, tile_id_t id)
            {
                if constexpr (Mode != Unsafe)
                    if ((pos < 0).any() || (pos >= size).any())
                        return;
                tiles[pos.x + size.x * pos.y].*layer = id;
            }

            template <SafetyMode Mode = Safe> Tile Get(ivec2 pos) const
            {
                if constexpr (Mode != Unsafe)
                    clamp_assign(pos, 0, size-1);
                return tiles[pos.x + size.x * pos.y];
            }
            template <SafetyMode Mode = Safe> tile_id_t Get(ivec2 pos, layer_mem_ptr_t layer) const
            {
                if constexpr (Mode != Unsafe)
                    clamp_assign(pos, 0, size-1);
                return tiles[pos.x + size.x * pos.y].*layer;
            }
        };

        Data data;

        ReflectStruct(ReflectedData, (
            (ivec2)(size),
            (std::vector<std::string>)(tile_names, variant_names),
            (std::vector<int>[layer_count])(layers),
        ))

      public:
        Map() {}
        Map(std::string file_name) : file_name(file_name)
        {
            // Trying to load the map from binary
            if (LoadFromFile())
                return;

            // Trying to load the map from forward-compatible format
            if (LoadFromFile(1))
            {
                SaveToFile();
                return;
            }

            data.size = ivec2(10);
            data.tiles.resize(data.size.product());
        }

        ivec2 Size() const
        {
            return data.size;
        }

        void Resize(ivec2 new_size, ivec2 offset)
        {
            if (new_size == data.size && offset == ivec2(0))
                return;
            if ((new_size < 1).any())
                return;

            Data new_data;
            new_data.size = new_size;
            new_data.tiles.resize(new_size.product());

            ivec2 a = max(ivec2(0), -offset), b = min(new_size - offset, data.size);

            for (int y = a.y; y < b.y; y++)
            for (int x = a.x; x < b.x; x++)
                new_data.Set<Unsafe>(ivec2(x,y) + offset, data.Get<Unsafe>(ivec2(x,y)));
            data = std::move(new_data);
        }

        void Set(ivec2 pos, const Tile &tile)
        {
            return data.Set(pos, tile);
        }
        void Set(ivec2 pos, layer_mem_ptr_t layer, tile_id_t id)
        {
            return data.Set(pos, layer, id);
        }

        Tile Get(ivec2 pos) const
        {
            return data.Get(pos);
        }
        tile_id_t Get(ivec2 pos, layer_mem_ptr_t layer) const
        {
            return data.Get(pos, layer);
        }

        // `tile_pos` is used only for visibility check.
        template <typename F> static void DrawTile(const Tiling::TileVariant &variant, ivec2 pos, ivec2 tile_pos, ivec2 first_visible, ivec2 last_visible, F &&func = [](Renderers::Poly2D::Quad_t &){})
        {
            if ((tile_pos + variant.offset + variant.size <= first_visible).any())
                return;
            if ((tile_pos + variant.offset > last_visible).any())
                return;

            auto quad = r.Quad(pos + variant.TextureOffset(), variant.TextureSize()).tex(variant.TexturePos());
            func(quad);
        }

        void Render(const Scene &scene, layer_mem_ptr_t layer, bool transparent = 0) const
        {
            constexpr int period = 120;

            auto &cam = scene.Get<Camera>();

            ivec2 first_visible = div_ex(cam.pos - screen_sz / 2, tile_size),
                  last_visible  = div_ex(cam.pos + screen_sz / 2, tile_size);

            ivec2 first = first_visible + tiling.MaxTextureOffsetNegative(),
                  last  = last_visible  + tiling.MaxTextureOffsetPositive();

            float t;
            if (transparent)
            {
                t = tick_stabilizer.ticks % period / float(period/2);
                t = (t < 1 ? smoothstep(t) : smoothstep(2-t));
                t *= 0.5;
            }


            for (int i = 0; i < 2; i++)
            {
                bool small_tiles = (i == 0);

                for (int y = first.y; y <= last.y; y++)
                for (int x = first.x; x <= last.x; x++)
                {

                    ivec2 pos = ivec2(x,y);

                    tile_id_t id = Get(pos, layer);
                    if (id == no_tile)
                        continue;

                    const auto &variant = tiling.GetVariant(id);

                    if (variant.Small() != small_tiles)
                        continue;

                    DrawTile(variant, pos * tile_size - cam.pos, pos, first_visible, last_visible, [&](Renderers::Poly2D::Quad_t &quad){if (transparent) quad.alpha(t);});
                }
            }
        }

        bool AnyTileExistsAt(ivec2 pos) const
        {
            auto tile = Get(pos);
            for (int layer = 0; layer < num_layers; layer++)
                if (tile.*layer_list[layer] != no_tile)
                    return 1;
            return 0;
        }
        bool TileExistsAt(int index, ivec2 pos) const
        {
            layer_mem_ptr_t layer = layer_list[tiling.TileByIndex(index).layer];
            tile_id_t tile_id = Get(pos, layer);
            if (tile_id == no_tile)
                return 0;
            return tiling.GetTileIndex(tile_id) == index;
        }
        bool TileFromGroupExistsAt(int group_index, ivec2 pos) const
        {
            const auto &group = tiling.GroupByIndex(group_index);
            auto tile = Get(pos);
            for (int layer = 0; layer < num_layers; layer++)
            {
                tile_id_t tile_id = tile.*layer_list[layer];
                if (tile_id != no_tile && group.Contains(tiling.GetTileIndex(tile_id)))
                    return 1;
            }
            return 0;
        }

        bool CheckTileRequirement(const Tiling::TileRule::Requirement &req, ivec2 pos) // Offset mentioned in the requirement is added to `pos`.
        {
            pos += req.offset;
            if (req.is_group)
            {
                return TileFromGroupExistsAt(req.index, pos);
            }
            else
            {
                if (req.index == -1)
                    return AnyTileExistsAt(pos);
                else
                    return TileExistsAt(req.index, pos);
            }
        }

        void RunAutotilerForOneTile(ivec2 pos)
        {
            for (int layer_index = 0; layer_index < num_layers; layer_index++)
            {
                layer_mem_ptr_t mem_ptr = layer_list[layer_index];
                tile_id_t tile_id = Get(pos, mem_ptr);
                if (tile_id == no_tile)
                    continue;
                int tile_index = tiling.GetTileIndex(tile_id);
                const auto &tile = tiling.TileByIndex(tile_index);

                int new_variant_index = tile.va_default_index;

                auto rules = tiling.GetTileRules(tile_index);
                for (const auto &rule : rules)
                {
                    if (!rule.CanBeAppliedToVariant(new_variant_index))
                        continue;

                    if (rule.modulo_pos.apply)
                    {
                        if (rule.modulo_pos.offsets.count(pos % rule.modulo_pos.size) == 0) // We don't need `mod_ex` here, position will never be negative anyway.
                            continue;
                    }

                    bool ok = 1;
                    for (const auto &req : rule.requires)
                    {
                        if (!CheckTileRequirement(req, pos))
                        {
                            ok = 0;
                            break;
                        }
                    }
                    if (!ok)
                        continue;
                    for (const auto &req_not : rule.requires_not)
                    {
                        if (CheckTileRequirement(req_not, pos))
                        {
                            ok = 0;
                            break;
                        }
                    }
                    if (!ok)
                        continue;

                    const auto &results = rule.results;

                    if (results.size() == 1)
                    {
                        new_variant_index = results[0].index;
                    }
                    else
                    {
                        float r = random_real(1);

                        bool selected = 0;

                        for (std::size_t i = 0; i < results.size() - 1; i++) // Sic! We don't iterate over the last element to avoid potential precision errors if `r == 1`.
                        {
                            const auto &result = results[i];
                            if (r <= result.chance)
                            {
                                new_variant_index = result.index;
                                selected = 1;
                                break;
                            }
                            else
                            {
                                r -= result.chance;
                            }
                        }
                        if (!selected)
                            new_variant_index = results.back().index;
                    }
                }

                Set(pos, mem_ptr, tile.variants[new_variant_index].global_index);
            }
        }
        void RunAutotiler(ivec2 pos, ivec2 size = ivec2(1)) // Runs autotiler for each tile in the specified rectange, expanded in every direction by `tiling.AutotilingRange()`.
        {
            ivec2 range = tiling.AutotilingRange();
            for (int y = -range.y; y < size.y + range.y; y++)
            for (int x = -range.x; x < size.x + range.x; x++)
                RunAutotilerForOneTile(pos + ivec2(x,y));
        }
        void RunAutotilerForEntireMap()
        {
            for (int y = 0; y <= data.size.y; y++)
            for (int x = 0; x <= data.size.x; x++)
                RunAutotilerForOneTile(ivec2(x,y));
        }

        const std::string &FileName() const
        {
            return file_name;
        }
        void SetFileName(std::string new_file_name)
        {
            file_name = new_file_name;
        }

        bool SaveToFile(bool forward_compat = 0, std::string suffix = "") const
        {
            ReflectedData refl;
            refl.size = data.size;
            for (tile_id_t tile_id = 0; tile_id < tiling.IndexCount(); tile_id++)
            {
                refl.tile_names.push_back(tiling.GetTile(tile_id).name);
                refl.variant_names.push_back(tiling.GetVariant(tile_id).name);
            }
            for (int la = 0; la < layer_count; la++)
            {
                auto &layer = refl.layers[la];
                layer.reserve(data.size.product());
                for (int y = 0; y < data.size.y; y++)
                for (int x = 0; x < data.size.x; x++)
                    layer.push_back(data.tiles[x + data.size.x * y].*layer_list[la]);
            }

            if (forward_compat)
            {
                std::string str = Reflection::to_string(refl);
                return Utils::WriteToFile(file_name + ".fwdcompat" + suffix, (uint8_t *)str.data(), str.size());
            }
            else
            {
                auto len = sizeof(uint32_t) + Reflection::byte_buffer_size(refl);
                auto buf = std::make_unique<uint8_t[]>(len);
                uint8_t *ptr = Reflection::to_bytes<uint32_t>(version_magic, buf.get());
                ptr = Reflection::to_bytes(refl, ptr);
                if (ptr != buf.get() + len)
                    return 0;
                return Utils::WriteToFile(file_name + suffix, buf.get(), len, Utils::compressed);
            }
        }
        bool LoadFromFile(bool forward_compat = 0)
        {
            Utils::MemoryFile file;
            try
            {
                if (forward_compat)
                    file.Create(file_name + ".fwdcompat");
                else
                    file.Create(file_name, Utils::compressed);
            }
            catch(decltype(Utils::file_input_error("","")) &e)
            {
                return 0;
            }

            ReflectedData refl;

            bool ok;
            if (forward_compat)
            {
                ok = Reflection::from_string(refl, (char *)file.Data()); // `MemoryFile::Data()` is null-terminated, so we're fine.
            }
            else
            {
                ok = 0;
                const uint8_t *begin = file.Data(), *end = file.Data() + file.Size();
                uint32_t magic;
                begin = Reflection::from_bytes<uint32_t>(magic, begin, end);
                if (begin && magic == version_magic)
                {
                    begin = Reflection::from_bytes(refl, begin, end);
                    if (begin == end)
                        ok = 1;
                }
            }

            if (!ok)
                return 0;

            if (refl.tile_names.size() != refl.variant_names.size())
                return 0;

            int indices_in_file = refl.tile_names.size();

            Data new_data; // There is no real need in operating on a copy for now, but we still do it.
            new_data.size = refl.size;
            new_data.tiles.resize(new_data.size.product());
            std::vector<tile_id_t> mapping;
            mapping.reserve(indices_in_file);
            for (int index = 0; index < indices_in_file; index++)
                mapping.push_back(tiling.IndexByName(refl.tile_names[index], refl.variant_names[index]));

            for (int la = 0; la < layer_count; la++)
            for (int y = 0; y < new_data.size.y; y++)
            for (int x = 0; x < new_data.size.x; x++)
            {
                int flat_xy = x + new_data.size.x * y;
                int old_index = refl.layers[la][flat_xy], new_index;
                if (old_index < 0 || old_index >= int(mapping.size()))
                    new_index = -1;
                else
                    new_index = mapping[old_index];
                new_data.tiles[flat_xy].*layer_list[la] = new_index;
            }

            data = std::move(new_data);

            return 1;
        }
    };

    class MapEditor
    {
        bool enabled = 0;
        ivec2 editor_cam_pos = ivec2(0);

        enum class OtherLayersHandling {show, transparent, hide};
        OtherLayersHandling other_layers_handling = OtherLayersHandling::show;

        bool selecting_tiles = 0;
        float selecting_tiles_alpha = 0;
        bool selecting_tiles_button_selected = 0;
        ivec2 selecting_tiles_selected_button_pos = ivec2(0);

        static constexpr ivec2 selecting_tiles_buttons_per_screen = ivec2(6,16);
        static constexpr int selecting_tiles_button_text_offset = 12;
        static constexpr ivec2 selecting_tiles_button_sz = screen_sz / selecting_tiles_buttons_per_screen;


        class MapInterface
        {
            ivec2 grabbed_size = ivec2(0);

            struct GrabbedLayer
            {
                Map::layer_mem_ptr_t layer;
                std::vector<Map::tile_id_t> tiles;
            };
            std::vector<GrabbedLayer> grabbed_layers;

            // Constructor sets those:
            Map::LayerEnum target_layer_enum = Map::num_layers;
            Map::layer_mem_ptr_t target_layer;

          public:
            MapInterface()
            {
                SelectAllLayers();
            }

            void SelectLayer(Map::LayerEnum layer_enum)
            {
                target_layer_enum = layer_enum;
                if (target_layer_enum != Map::num_layers)
                    target_layer = Map::layer_list[target_layer_enum];
                else
                    target_layer = 0;
            }
            void SelectAllLayers()
            {
                SelectLayer(Map::num_layers);
            }

            int AvailableTileCount() const
            {
                if (target_layer)
                    return int(Map::tiling.TileIndicesForLayer(target_layer_enum).size());
                else
                    return Map::tiling.TileCount();
            }
            int AvailableTileIndex(int avail_index) const // Returns the global index corresponding to an index in the list of available tiles.
            {
                if (target_layer)
                    return Map::tiling.TileIndicesForLayer(target_layer_enum)[avail_index];
                else
                    return avail_index;
            }

            void GrabTile(int avail_index) // The index is used for the list of tiles available for the current layer.
            {
                int tile_index = AvailableTileIndex(avail_index);

                grabbed_size = ivec2(1);
                grabbed_layers = {};

                GrabbedLayer new_grabbed_layer;
                new_grabbed_layer.layer = Map::layer_list[Map::tiling.TileByIndex(tile_index).layer];
                new_grabbed_layer.tiles = {Map::tiling.DefaultVariant(tile_index).global_index};

                grabbed_layers.push_back(new_grabbed_layer);
            }
            void GrabArea(const Map &map, ivec2 a, ivec2 b)
            {
                if (a.x > b.x) std::swap(a.x, b.x);
                if (a.y > b.y) std::swap(a.y, b.y);

                grabbed_size = b - a + 1;
                grabbed_layers = {};

                auto GrabLayer = [&](Map::layer_mem_ptr_t layer)
                {
                    GrabbedLayer new_grabbed_layer;

                    new_grabbed_layer.layer = layer;
                    new_grabbed_layer.tiles.resize(grabbed_size.product());

                    for (int y = 0; y < grabbed_size.y; y++)
                    for (int x = 0; x < grabbed_size.x; x++)
                    {
                        Map::tile_id_t tile_id = map.Get(ivec2(x,y) + a, layer);
                        if (tile_id != Map::no_tile)
                            tile_id = Map::tiling.DefaultVariant(Map::tiling.GetTileIndex(tile_id)).global_index;
                        new_grabbed_layer.tiles[x + y * grabbed_size.x] = tile_id;
                    }

                    grabbed_layers.push_back(new_grabbed_layer);
                };

                if (target_layer)
                {
                    GrabLayer(target_layer);
                }
                else
                {
                    for (int layer_index = 0; layer_index < Map::num_layers; layer_index++)
                        GrabLayer(Map::layer_list[layer_index]);
                }
            }
            void ReleaseGrab()
            {
                grabbed_size = ivec2(0);
                grabbed_layers = {};
            }
            bool Grabbed() const
            {
                return grabbed_layers.size() != 0;
            }
            ivec2 GrabbedSize() const
            {
                return grabbed_size;
            }

            void PasteGrabbed(Map &map, ivec2 offset) const
            {
                for (const auto &grabbed_layer : grabbed_layers)
                for (int y = 0; y < grabbed_size.y; y++)
                for (int x = 0; x < grabbed_size.x; x++)
                    map.Set(ivec2(x,y) + offset, grabbed_layer.layer, grabbed_layer.tiles[x + grabbed_size.x * y]);

                map.RunAutotiler(offset, grabbed_size);
            }

            void RenderGrabbed(ivec2 cam_pos)
            {
                constexpr int period = 60, air_margin = 1;
                constexpr float highlight = 1/3., alpha = 3/4., air_alpha = 1/4.;


                ivec2 first_visible = div_ex(cam_pos - screen_sz / 2, tile_size),
                      last_visible  = div_ex(cam_pos + screen_sz / 2, tile_size);

                ivec2 first = first_visible + Map::tiling.MaxTextureOffsetNegative(),
                      last  = last_visible  + Map::tiling.MaxTextureOffsetPositive();

                ivec2 render_offset = max(first - grab_offset, 0),
                      render_size   = min(last - first + 1, grabbed_size);

                float t = tick_stabilizer.ticks % period / float(period/2);
                t = (t < 1 ? smoothstep(t) : smoothstep(2-t));

                for (const auto &grabbed_layer : grabbed_layers)
                {
                    for (int i = 0; i < 3; i++)
                    {
                        bool air_only    = (i == 0),
                             small_tiles = (i == 1);

                        for (int y = render_offset.y; y < render_size.y; y++)
                        for (int x = render_offset.x; x < render_size.x; x++)
                        {
                            ivec2 pos = ivec2(x,y);

                            Map::tile_id_t id = grabbed_layer.tiles[x + grabbed_size.x * y];

                            if (air_only && id == Map::no_tile)
                            {
                                r.Quad((pos + grab_offset) * tile_size - cam_pos + air_margin, ivec2(tile_size-air_margin*2)).color(fvec3(t)).alpha(air_alpha);
                            }
                            if (!air_only && id != Map::no_tile)
                            {
                                const auto &variant = Map::tiling.GetVariant(id);

                                if (variant.Small() != small_tiles)
                                    continue;

                                Map::DrawTile(variant, (pos + grab_offset) * tile_size - cam_pos, pos + grab_offset, first_visible, last_visible, [&](Renderers::Poly2D::Quad_t &quad){quad.color(fvec3(t)).mix(1-highlight).alpha(alpha);});
                            }
                        }
                    }
                }
            }

            ivec2 grab_offset = ivec2(0);


            void EraseArea(Map &map, ivec2 a, ivec2 b)
            {
                if (a.x > b.x) std::swap(a.x, b.x);
                if (a.y > b.y) std::swap(a.y, b.y);

                auto Erase = [&](Map::layer_mem_ptr_t layer)
                {
                    for (int y = a.y; y <= b.y; y++)
                    for (int x = a.x; x <= b.x; x++)
                        map.Set(ivec2(x,y), layer, Map::no_tile);
                };

                if (target_layer)
                {
                    Erase(target_layer);
                }
                else
                {
                    for (int layer_index = 0; layer_index < Map::num_layers; layer_index++)
                        Erase(Map::layer_list[layer_index]);
                }

                map.RunAutotiler(a, b - a + 1);
            }

            bool LayerSelected(Map::LayerEnum layer_enum) const
            {
                return target_layer_enum == layer_enum || AllLayersSelected();
            }
            bool LayerSelected(Map::layer_mem_ptr_t layer) const
            {
                return target_layer == layer || AllLayersSelected();
            }
            bool AllLayersSelected() const
            {
                return target_layer_enum == Map::num_layers;
            }
        };
        MapInterface map_interface;


        ivec2 mouse_pos = ivec2(0);

        ivec2 map_selection_start = ivec2(0),
              map_selection_end = ivec2(0);
        bool map_selection_button_down = 0,
             map_selection_map_hovered = 0,
             map_selection_multiple_tiles = 0;

        bool eraser_mode = 0;

        bool resize_started = 0;
        ivec2 resize_type = ivec2(0);
        std::string resize_string;

        bool show_help = 1;

        std::string message_text;
        float message_alpha = 0;

        void ShowMessage(std::string text)
        {
            message_text = text;
            message_alpha = 1;
        }

        void SaveMap(const Scene &scene, bool forward_compat = 0)
        {
            auto &map = scene.Get<Map>();
            bool ok = map.SaveToFile(forward_compat);
            if (ok)
                ShowMessage(Str("\2Map \1", map.FileName(), "\2 was successfully saved", (forward_compat ? " \4(compatibility mode)" : "")));
            else
                ShowMessage(Str("\3Map \1", map.FileName(), "\3 couldn't be saved", (forward_compat ? " \4(compatibility mode)" : "")));
        }
        void LoadMap(const Scene &scene, bool forward_compat = 0)
        {
            auto &map = scene.Get<Map>();
            bool ok = map.LoadFromFile(forward_compat);
            if (ok)
                ShowMessage(Str("\2Map \1", map.FileName(), "\2 was successfully loaded", (forward_compat ? " \4(compatibility mode)" : "")));
            else
                ShowMessage(Str("\3Map \1", map.FileName(), "\3 couldn't be loaded", (forward_compat ? " \4(compatibility mode)" : "")));
        }

      public:
        void Enable(const Scene &scene, bool e = 1)
        {
            enabled = e;
            if (enabled)
            {
                editor_cam_pos = scene.Get<Camera>().pos;
            }
        }

        void Tick(const Scene &scene)
        {
            auto &map = scene.Get<Map>();
            auto &cam = scene.Get<Camera>();

            { // Save on exit
                if (Events::ExitRequested())
                    SaveMap(scene);
            }

            { // Timers
                if (message_alpha > 0)
                {
                    message_alpha -= 0.01;
                    if (message_alpha < 0)
                        message_alpha = 0;
                }
            }

            if (resize_started) // Actually resizing
            {
                Input::Text(&resize_string, 5, "-0123456789");
                if (Keys::enter.pressed())
                {
                    resize_started = 0;
                    int value = 0;
                    Reflection::Interface::primitive_from_string(value, resize_string.c_str());
                    resize_string = "";

                    ivec2 new_size = map.Size() + abs(resize_type) * value;
                    ivec2 offset = abs((resize_type < 0).to<int>()) * value;
                    map.Resize(new_size, offset);
                    editor_cam_pos += offset * tile_size;
                }
                else
                    return;
            }

            { // Show/hide GUI
                if (Keys::grave.pressed())
                {
                    Enable(scene, !enabled);
                    if (!enabled)
                        SaveMap(scene);
                }
                if (!enabled)
                    return;
            }

            { // Reload stuff if needed
                if (Keys::f5.pressed())
                {
                    Draw::ReloadTextures();
                    SaveMap(scene);
                    Map::tiling.Reload();
                    LoadMap(scene);
                }
            }

            if (enabled)
            {
                { // Saving/loading the map
                    if (Keys::space.pressed())
                        SaveMap(scene, Keys::l_alt.down());
                    if (Keys::l_ctrl.down() && Keys::f5.pressed())
                        LoadMap(scene, Keys::l_alt.down());
                }

                { // Rerunning autotiler
                    if (Keys::f12.pressed())
                        map.RunAutotilerForEntireMap();
                }

                { // Open/close tile list
                    if (!selecting_tiles && Keys::tab.pressed() && !map_selection_button_down)
                    {
                        selecting_tiles = 1;
                        eraser_mode = 0;
                    }
                    else if (selecting_tiles && Keys::tab.pressed())
                        selecting_tiles = 0;
                    selecting_tiles_alpha = clamp(selecting_tiles_alpha + (selecting_tiles ? 1 : -1) * 0.12, 0, 1);
                }

                { // Show/hide help text
                    if (Keys::f1.pressed())
                        show_help = !show_help;
                }

                ivec2 wasd = ivec2(Keys::d.down() - Keys::a.down(), Keys::s.down() - Keys::w.down());
                { // WASD modifiers
                    int speed = 6;
                    if (Keys::l_alt.down())
                        speed = 100;
                    if (Keys::l_ctrl.down())
                        speed = 20;
                    if (Keys::l_shift.down())
                        speed = 1;
                    wasd *= speed;
                }

                { // Camera
                    clamp_assign(editor_cam_pos += wasd, 0, map.Size() * tile_size);

                    cam.pos = editor_cam_pos;
                }

                { // Switching modes
                    if (Keys::e.pressed())
                    {
                        eraser_mode = !eraser_mode;
                        map_interface.ReleaseGrab();
                    }
                }

                { // Switching layers
                    if (Keys::_1.pressed())
                        map_interface.SelectLayer(Map::la_back);
                    if (Keys::_2.pressed())
                        map_interface.SelectLayer(Map::la_mid);
                    if (Keys::_3.pressed())
                        map_interface.SelectLayer(Map::la_front);
                    if (Keys::_4.pressed())
                        map_interface.SelectAllLayers();
                }

                { // Switching layer transparency
                    if (Keys::z.pressed())
                        other_layers_handling = OtherLayersHandling::hide;
                    if (Keys::x.pressed())
                        other_layers_handling = OtherLayersHandling::transparent;
                    if (Keys::c.pressed())
                        other_layers_handling = OtherLayersHandling::show;
                }

                // Selecting tiles
                if (selecting_tiles)
                {
                    selecting_tiles_button_selected = 1;
                    selecting_tiles_selected_button_pos = div_ex(mouse.pos() + screen_sz/2, selecting_tiles_button_sz);

                    int button_index = selecting_tiles_selected_button_pos.y + selecting_tiles_buttons_per_screen.y * selecting_tiles_selected_button_pos.x;

                    if ((selecting_tiles_selected_button_pos < 0).any() || (selecting_tiles_selected_button_pos >= selecting_tiles_buttons_per_screen).any() ||
                         button_index >= map_interface.AvailableTileCount())
                    {
                        selecting_tiles_button_selected = 0;
                        selecting_tiles_selected_button_pos = ivec2(-1);
                    }

                    if (mouse.left.released() && selecting_tiles_button_selected)
                    {
                        selecting_tiles = 0;
                        map_interface.GrabTile(button_index);
                    }
                }
                else // Editing the map
                {
                    { // Starting resize
                        if (Keys::l_alt.down())
                        {
                            if (Keys::left.pressed())
                            {
                                resize_started = 1;
                                resize_type = ivec2(-1,0);
                            }
                            if (Keys::right.pressed())
                            {
                                resize_started = 1;
                                resize_type = ivec2(1,0);
                            }
                            if (Keys::up.pressed())
                            {
                                resize_started = 1;
                                resize_type = ivec2(0,-1);
                            }
                            if (Keys::down.pressed())
                            {
                                resize_started = 1;
                                resize_type = ivec2(0,1);
                            }
                        }
                    }

                    bool mouse_pos_changed = 0;
                    { // Calculating hovered tile
                        ivec2 mouse_pos_prev = mouse_pos;
                        mouse_pos = div_ex(mouse.pos() + cam.pos, tile_size);
                        if (mouse_pos != mouse_pos_prev)
                            mouse_pos_changed = 1;

                        map_selection_map_hovered = (mouse_pos >= 0).all() && (mouse_pos < map.Size()).all();
                    }

                    // Selecting map region
                    if (!map_interface.Grabbed() && mouse.right.pressed() && map_selection_map_hovered)
                    {
                        map_selection_button_down = 1;
                        map_selection_start = clamp(mouse_pos, 0, map.Size()-1);
                        map_selection_multiple_tiles = 0;
                    }
                    if (map_selection_button_down)
                    {
                        map_selection_end = clamp(mouse_pos, 0, map.Size()-1);

                        if (!map_selection_multiple_tiles && map_selection_end != map_selection_start)
                            map_selection_multiple_tiles = 1;

                        if (mouse.right.released())
                        {
                            map_selection_button_down = 0;

                            ivec2 a = map_selection_start,
                                  b = map_selection_end;

                            if (!eraser_mode)
                                map_interface.GrabArea(map, a, b);
                            else
                                map_interface.EraseArea(map, a, b);
                        }
                    }

                    // Moving and placing grabbed tiles
                    if (!eraser_mode && map_interface.Grabbed())
                    {
                        ivec2 grab_offset_prev = map_interface.grab_offset;
                        map_interface.grab_offset = div_ex(-map_interface.GrabbedSize() * tile_size/2 + mouse.pos() + cam.pos + tile_size/2, tile_size);

                        if (mouse.left.pressed() || (mouse.left.down() && map_interface.grab_offset != grab_offset_prev))
                            map_interface.PasteGrabbed(map, map_interface.grab_offset);

                        if (mouse.right.pressed())
                            map_interface.ReleaseGrab();
                    }

                    // Erasing
                    if (mouse.left.down() && !map_interface.Grabbed() && !map_selection_button_down && (mouse_pos_changed || mouse.left.pressed()) && map_selection_map_hovered)
                        map_interface.EraseArea(map, mouse_pos, mouse_pos);
                }
            }
        }
        void Render(const Scene &scene)
        {
            auto &map = scene.Get<Map>();
            auto &cam = scene.Get<Camera>();

            if (enabled)
            {
                constexpr int period = 60;

                float t = tick_stabilizer.ticks % period / float(period/2);
                t = (t < 1 ? smoothstep(t) : smoothstep(2-t));

                fvec3 mode_color = fvec3(0.5);
                if (!eraser_mode)
                    mode_color = fvec3(0,0.6,1);
                else
                    mode_color = fvec3(1,0.2,0.2);

                // Grabbed tiles
                if (!selecting_tiles && map_interface.Grabbed())
                    map_interface.RenderGrabbed(cam.pos);

                { // Map border
                    constexpr int width = 4;
                    for (int i = 0; i <= 1; i++)
                        Draw::Rect(ivec2(width * -i) - cam.pos, map.Size() * tile_size + width, width, fvec3(i), 0.5);
                }


                // Map selection
                bool show_selection_rect = map_selection_button_down && (map_selection_multiple_tiles || eraser_mode);
                if (show_selection_rect)
                {
                    constexpr int width = 2;

                    ivec2 a = map_selection_start,
                          b = map_selection_end;
                    if (a.x > b.x) std::swap(a.x, b.x);
                    if (a.y > b.y) std::swap(a.y, b.y);

                    Draw::Rect(a * tile_size - cam.pos, (b - a + 1) * tile_size, width*3, fvec3(1));

                    for (int i = 0; i <= 1; i++)
                        Draw::Rect(a * tile_size - cam.pos - ivec2(i * width), (b - a + 1) * tile_size + width, width, i ? mode_color : fvec3(0));
                }

                // Mouse cursor
                if (!selecting_tiles && !map_interface.Grabbed() && !show_selection_rect)
                {
                    constexpr int cursor_width = 4, cursor_length = 4, cursor_corner_size = 4, cursor_outline_size = 2;

                    Draw::Rect(mouse_pos * tile_size - cam.pos, ivec2(tile_size), cursor_outline_size, fvec3(1));

                    int i = 0;
                    for (const auto &m : {fmat2(1,0,0,1),fmat2(0,-1,1,0),fmat2(-1,0,0,-1),fmat2(0,1,-1,0)})
                    {
                        r.Quad(mouse_pos * tile_size + tile_size/2 - cam.pos, ivec2(1, tile_size)).center(ivec2(1+tile_size/2,tile_size/2)).color(mode_color * t).matrix(m);
                        r.Quad(mouse_pos * tile_size + tile_size/2 - cam.pos, ivec2(cursor_length, cursor_width)).center(ivec2(cursor_length+tile_size/2, cursor_width/2)).color(mode_color * t).matrix(m);
                        r.Quad(mouse_pos * tile_size + tile_size/2 - cam.pos, ivec2(cursor_corner_size)).center(ivec2(cursor_corner_size+tile_size/2)).color(mode_color * (1-t)).matrix(m);
                        i++;
                    }
                }

                { // Text
                    // Top left
                    std::string top_left = "Editing: tiles";

                    // Bottom left
                    ivec2 cam_pos = div_ex(cam.pos, tile_size),
                          pos = (map_interface.Grabbed() ? map_interface.grab_offset : mouse_pos);
                    std::string bottom_left = Str("Map size: [", std::setw(5), map.Size().x, ",", std::setw(5), map.Size().y, "]",
                                                  "     Camera center: [", std::setw(5), cam_pos.x, ",", std::setw(5), cam_pos.y, "]",
                                                  "          Pos: [", std::setw(5), pos.x, ",", std::setw(5), pos.y, "]");
                    ivec2 size(0);
                    if (map_interface.Grabbed())
                        size = map_interface.GrabbedSize();
                    else if (map_selection_button_down && map_selection_multiple_tiles)
                        size = abs(map_selection_end - map_selection_start) + 1;
                    if (size != ivec2(0))
                        bottom_left += Str("          Size: [", std::setw(3), size.x, ",", std::setw(3), size.y, "]");

                    // Bottom right
                    std::string bottom_right;
                    {
                        if (show_help)
                        {
                            bottom_right = "WASD to move\n"
                                           "(+SHIFT - slow, +CTRL - fast, +ALT - faster)\n"
                                           "TAB to open tile sheet\n"
                                           "LMB to draw or erase\n"
                                           "RMB to select tiles\n"
                                           "E to switch to eraser mode\n"
                                           "1,2,3,4 to change layer\n"
                                           "Z,X,C to change visiblity of other layers\n"
                                           "ALT+<^>v to resize map\n"
                                           "F5 to reload textures and tiling settings\n"
                                           "SPACE to save\n"
                                           "(+ALT to save/load in forward-compatible mode)\n"
                                           "CTRL+F5 to reload\n"
                                           "F12 to rerun autotiler\n"
                                           "F1 to hide this text";
                        }
                        else
                            bottom_right = "F1 to show help";
                    }

                    // Top middle
                    std::string top_middle;
                    {
                        if (!selecting_tiles && eraser_mode)
                            top_middle = "Erasing";
                    }

                    // Top right
                    std::string top_right;
                    {
                        char color;
                        if (map_interface.AllLayersSelected())
                            color = '\2';
                        else
                            color = '\4';

                        top_right = Str((map_interface.LayerSelected(Map::back ) ? color : '\r'), "Back\n",
                                        (map_interface.LayerSelected(Map::mid  ) ? color : '\r'), "Middle\n",
                                        (map_interface.LayerSelected(Map::front) ? color : '\r'), "Front\n");
                    }

                    // Top right 2
                    std::string top_right_2;
                    {
                        top_right_2 = Str("\1Other layers:\n\n",
                                          "\r\5"[other_layers_handling == OtherLayersHandling::hide       ], "Hidden\n",
                                          "\r\5"[other_layers_handling == OtherLayersHandling::transparent], "Transparent\n",
                                          "\r\5"[other_layers_handling == OtherLayersHandling::show       ], "Shown\n");
                    }

                    // Render
                    r.Text(-screen_sz/2 + 2                   , top_left     ).preset(Draw::WithBlackOutline).font(font_tiny).align({-1,-1});
                    r.Text((screen_sz/2-2).mul_x(-1)          , bottom_left  ).preset(Draw::WithBlackOutline).font(font_tiny).align({-1,1});
                    r.Text(screen_sz/2 - 2                    , bottom_right ).preset(Draw::WithBlackOutline).font(font_tiny).align({1,1});
                    r.Text(ivec2(0,-screen_sz.y/2+20)         , top_middle   ).preset(Draw::WithBlackOutline).align({0,0}).color(mode_color);
                    r.Text((screen_sz/2-2).mul_y(-1)          , top_right    ).preset(Draw::WithColors()).preset(Draw::WithBlackOutline).align({1,-1});
                    r.Text((screen_sz/2-2).mul_y(-1).add_y(72), top_right_2  ).preset(Draw::WithColors()).preset(Draw::WithBlackOutline).font(font_tiny).align({1,-1});
                }

                // Tile selector
                if (selecting_tiles_alpha)
                {
                    constexpr int rect_margin = 2, rect_line_width = 1;

                    // Dark background
                    r.Quad(-screen_sz/2, screen_sz).color(fvec3(0)).alpha(0.8 * selecting_tiles_alpha);

                    int count = map_interface.AvailableTileCount();

                    // Buttons
                    int button_index = 0;
                    for (int x = 0; x < selecting_tiles_buttons_per_screen.x && button_index < count; x++)
                    for (int y = 0; y < selecting_tiles_buttons_per_screen.y && button_index < count; y++)
                    {
                        ivec2 base = selecting_tiles_button_sz * ivec2(x,y) - screen_sz/2;
                        int tile_index = map_interface.AvailableTileIndex(button_index);

                        // Selection
                        if (selecting_tiles_button_selected && ivec2(x,y) == selecting_tiles_selected_button_pos)
                        {
                            int extra_width = mouse.left.down();
                            Draw::Rect(base + ivec2(rect_margin - 1 + extra_width), selecting_tiles_button_sz - ivec2(2*(rect_margin - 1 + extra_width)), rect_line_width              , fvec3(0   ), selecting_tiles_alpha);
                            Draw::Rect(base + ivec2(rect_margin     + extra_width), selecting_tiles_button_sz - ivec2(2*(rect_margin     + extra_width)), rect_line_width + extra_width, fvec3(0.75), selecting_tiles_alpha);
                        }

                        // Block icon
                        // (We intentionally draw only a single tile, even if the tile texture is larger.)
                        r.Quad(base + selecting_tiles_button_sz.y/2, ivec2(tile_size)).tex(Map::tiling.DefaultVariant(tile_index).texture * tile_size + Map::sheet_tex_pos).alpha(selecting_tiles_alpha).center();

                        // Text
                        r.Text(base + selecting_tiles_button_sz.y/2 + ivec2(selecting_tiles_button_text_offset,0), Map::tiling.TileByIndex(tile_index).name)
                         .preset(Draw::WithBlackOutline).align_h(-1).alpha(selecting_tiles_alpha).font(font_tiny);

                        button_index++;
                    }
                }
            }

            { // Map resizing gui
                if (resize_started)
                {
                    r.Quad(ivec2(0), ivec2(screen_sz.x, 60)).color(fvec3(0)).center();
                    std::string msg = "Extent \1";
                         if (resize_type == ivec2(-1,0)) msg += "left";
                    else if (resize_type == ivec2( 1,0)) msg += "right";
                    else if (resize_type == ivec2(0,-1)) msg += "top";
                    else/*(resize_type == ivec2(0, 1))*/ msg += "bottom";
                    msg += "\r border by:\n\n";
                    r.Text(ivec2(0), msg).preset(Draw::WithColors());
                    r.Text(ivec2(0), '\4'+resize_string).preset(Draw::WithColors()).preset(Draw::WithCursor(1+Input::TextCursorPos()));
                    int value = 0;
                    Reflection::Interface::primitive_from_string(value, resize_string.c_str());
                    ivec2 new_size = map.Size() + abs(resize_type)*value;
                    bool invalid_size = (new_size < 1).any();
                    r.Text(ivec2(0), Str("\n\nNew map size: \1", (!invalid_size ? Str(new_size) : "???"))).preset(Draw::WithColors());
                }
            }

            { // Messages
                if (message_alpha > 0)
                {
                    float a = min(1, 10 * min(message_alpha, 1 - message_alpha));
                    r.Quad(ivec2(0,-screen_sz.y/3), ivec2(screen_sz.x,30)).color(fvec3(0)).alpha(a).center();
                    r.Text(ivec2(0,-screen_sz.y/3), message_text).color(fvec3(0.5)).preset(Draw::WithColors()).alpha(a);
                }
            }
        }

        bool ShouldShowLayer(Map::layer_mem_ptr_t layer)
        {
            return other_layers_handling != OtherLayersHandling::hide || map_interface.LayerSelected(layer);
        }
        bool ShouldMakeLayerTransparent(Map::layer_mem_ptr_t layer)
        {
            return other_layers_handling == OtherLayersHandling::transparent && !map_interface.LayerSelected(layer);
        }
    };

    class MapRenderer
    {
      public:
        void Render(const Scene &scene, Map::layer_mem_ptr_t layer)
        {
            auto &map = scene.Get<Map>();
            bool visible = 1, transparent = 0;
            if (auto map_ed = scene.GetOpt<MapEditor>())
            {
                visible     = map_ed->ShouldShowLayer(layer);
                transparent = map_ed->ShouldMakeLayerTransparent(layer);
            }

            if (!visible)
                return;
            map.Render(scene, layer, transparent);
        }
    };


    class TestObject
    {
        std::string str = "Hello, world!";
      public:
        void Tick(const Scene &scene)
        {
            (void)scene;
        }
        void Render(const Scene &scene)
        {
            (void)scene;
        }
    };
}

namespace Scenes
{
    using namespace Objects;

    const Scene game = []
    {
        bool map_editor = 1;

        Scene s;
        s.Add<Camera>(ivec2(0));
        s.Add<Background>();
        s.Add<Map>("test.map");
        if (map_editor) s.Add<MapEditor>();
        s.Add<MapRenderer>();

        s.Add<TestObject>();

        s.Get<Camera>().pos = s.Get<Map>().Size() * tile_size / 2;
        if (map_editor) s.Get<MapEditor>().Enable(s);

        s.SetTick([](const Scene &s)
        {
            s.Get<Background>().Tick();
            if (auto ptr = s.GetOpt<MapEditor>()) ptr->Tick(s);

            s.Get<TestObject>().Tick(s);
        });
        s.SetRender([](const Scene &s)
        {
            s.Get<Background>().Render();
            s.Get<MapRenderer>().Render(s, Map::back);
            s.Get<MapRenderer>().Render(s, Map::mid);
            s.Get<MapRenderer>().Render(s, Map::front);
            if (auto ptr = s.GetOpt<MapEditor>()) ptr->Render(s);

            s.Get<TestObject>().Render(s);
        });
        return s;
    }();
}

Scene current_scene = Scenes::game;

int main(int, char **)
{
    Draw::Init();

    auto Tick = [&]
    {
        current_scene.Tick();
    };
    auto Render = [&]
    {
        Graphics::Clear(Graphics::color);
        current_scene.Render();
    };

    uint64_t frame_start = Timing::Clock(), frame_delta;

    while (1)
    {
        uint64_t time = Timing::Clock();
        frame_delta = time - frame_start;
        frame_start = time;

        while (tick_stabilizer.Tick(frame_delta))
        {
            Events::Process();
            if (win.size_changed)
            {
                win.size_changed = 0;
                Draw::Resize();
            }
            Tick();
        }

        Graphics::CheckErrors();

        framebuffer_main.Bind();
        r.BindShader();
        Graphics::Viewport(screen_sz);

        Render();
        r.Finish();

        framebuffer_scaled.Bind();
        Shaders::Identity::object.Bind();
        Shaders::Identity::uni.texture = texture_fbuf_main;
        Graphics::Viewport(texture_fbuf_scaled.Size());

        Draw::FullscreenQuad();

        Graphics::FrameBuffer::Unbind();
        Shaders::Identity::uni.texture = texture_fbuf_scaled;
        Graphics::Viewport((win.Size() - Draw::scaled_size) / 2, Draw::scaled_size);

        Draw::FullscreenQuad();

        win.Swap();
    }

    return 0;
}
