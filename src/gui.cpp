#include "gui.h"

#include <SDL2/SDL.h>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
    constexpr int kInitialWindowWidth = 1020;
    constexpr int kInitialWindowHeight = 760;

    struct GuiSnapshot
    {
        boardType board;
        playerColor current_player = playerColor::white;
        bool white_checked = false;
        bool black_checked = false;
        bool white_checkmate = false;
        bool black_checkmate = false;
        std::size_t move_count = 0;
        bool has_last_move = false;
        boardCoordinateType last_move_start{'A', 1};
        boardCoordinateType last_move_dest{'A', 1};
    };

    struct ButtonSpec
    {
        ChessGuiActionType action;
        const char *label;
    };

    struct PaletteSpec
    {
        pieceType piece;
        const char *label;
    };

    enum class TextInputField
    {
        none,
        editor_save_name,
        network_username,
        network_host,
        network_password
    };

    struct MenuSpec
    {
        ChessGuiActionType action;
        const char *label;
    };

    constexpr std::array<MenuSpec, 5> kMenuItems{{
        {ChessGuiActionType::start_new_game, "New Game"},
        {ChessGuiActionType::board_editor, "Board Editor"},
        {ChessGuiActionType::load_from_database, "Load DB"},
        {ChessGuiActionType::play_current_board, "Play Current"},
        {ChessGuiActionType::start_network_game, "Network"},
    }};

    constexpr std::array<ButtonSpec, 7> kButtons{{
        {ChessGuiActionType::smart_move, "Smart"},
        {ChessGuiActionType::random_move, "Random"},
        {ChessGuiActionType::undo, "Undo"},
        {ChessGuiActionType::ml_move, "ML Move"},
        {ChessGuiActionType::list_moves, "Moves"},
        {ChessGuiActionType::write_db, "Save"},
        {ChessGuiActionType::quit_game, "Quit"},
    }};

    constexpr std::array<PaletteSpec, 13> kEditorPalette{{
        {{pieceCode::pawn, playerColor::white}, "White Pawn"},
        {{pieceCode::knight, playerColor::white}, "White Knight"},
        {{pieceCode::bishop, playerColor::white}, "White Bishop"},
        {{pieceCode::rook, playerColor::white}, "White Rook"},
        {{pieceCode::queen, playerColor::white}, "White Queen"},
        {{pieceCode::king, playerColor::white}, "White King"},
        {{pieceCode::pawn, playerColor::black}, "Black Pawn"},
        {{pieceCode::knight, playerColor::black}, "Black Knight"},
        {{pieceCode::bishop, playerColor::black}, "Black Bishop"},
        {{pieceCode::rook, playerColor::black}, "Black Rook"},
        {{pieceCode::queen, playerColor::black}, "Black Queen"},
        {{pieceCode::king, playerColor::black}, "Black King"},
        {{pieceCode::empty, playerColor::none}, "Eraser"},
    }};

    constexpr std::array<ButtonSpec, 4> kEditorButtons{{
        {ChessGuiActionType::editor_clear_board, "Clear"},
        {ChessGuiActionType::editor_default_board, "Default"},
        {ChessGuiActionType::editor_save_board, "Save"},
        {ChessGuiActionType::editor_back, "Back"},
    }};

    constexpr std::array<ButtonSpec, 6> kDatabaseButtons{{
        {ChessGuiActionType::database_selection_changed, "Prev Game"},
        {ChessGuiActionType::database_selection_changed, "Next Game"},
        {ChessGuiActionType::database_selection_changed, "Prev Board"},
        {ChessGuiActionType::database_selection_changed, "Next Board"},
        {ChessGuiActionType::database_load_snapshot, "Load"},
        {ChessGuiActionType::database_back, "Back"},
    }};

    constexpr std::array<const char *, 2> kNetworkRoleLabels{{"Host", "Join"}};
    constexpr std::array<const char *, 2> kNetworkColorLabels{{"White", "Black"}};
    constexpr std::array<ButtonSpec, 2> kNetworkButtons{{
        {ChessGuiActionType::network_submit, "Start"},
        {ChessGuiActionType::network_back, "Back"},
    }};

    struct Layout
    {
        SDL_Rect menu_bar_rect{};
        std::array<SDL_Rect, kMenuItems.size()> menu_item_rects{};
        SDL_Rect board_rect{};
        int square_size = 0;
        SDL_Rect panel_rect{};
        SDL_Rect info_rect{};
        std::array<SDL_Rect, kButtons.size()> button_rects{};
        SDL_Rect footer_rect{};
    };

    struct GlyphKey
    {
        std::uint32_t codepoint = 0;
        int pixel_size = 0;

        bool operator<(const GlyphKey &other) const
        {
            return std::tie(pixel_size, codepoint) < std::tie(other.pixel_size, other.codepoint);
        }
    };

    struct GlyphTexture
    {
        SDL_Texture *texture = nullptr;
        int width = 0;
        int height = 0;
        int left = 0;
        int top = 0;
        int advance = 0;
    };

    struct TextMetrics
    {
        int width = 0;
        int height = 0;
        int ascent = 0;
        int descent = 0;
    };

    boardType make_empty_board()
    {
        boardType board{};
        for (auto &file : board)
        {
            for (auto &square : file)
            {
                square = {pieceCode::empty, playerColor::none};
            }
        }
        return board;
    }

    SDL_Color make_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255)
    {
        return SDL_Color{r, g, b, a};
    }

    void set_draw_color(SDL_Renderer *renderer, SDL_Color color)
    {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    }

    void fill_rect(SDL_Renderer *renderer, const SDL_Rect &rect, SDL_Color color)
    {
        set_draw_color(renderer, color);
        SDL_RenderFillRect(renderer, &rect);
    }

    void draw_rect(SDL_Renderer *renderer, const SDL_Rect &rect, SDL_Color color)
    {
        set_draw_color(renderer, color);
        SDL_RenderDrawRect(renderer, &rect);
    }

    bool point_in_rect(int x, int y, const SDL_Rect &rect)
    {
        return x >= rect.x && x < (rect.x + rect.w) &&
               y >= rect.y && y < (rect.y + rect.h);
    }

    bool same_square(boardCoordinateType left, boardCoordinateType right)
    {
        return left.file == right.file && left.rank == right.rank;
    }

    std::string square_name(boardCoordinateType square)
    {
        return std::string(1, square.file) + std::to_string(square.rank);
    }

    std::string piece_symbol_utf8(pieceType piece)
    {
        if (piece.color == playerColor::white)
        {
            switch (piece.piece)
            {
            case pieceCode::king:
                return u8"\u2654";
            case pieceCode::queen:
                return u8"\u2655";
            case pieceCode::rook:
                return u8"\u2656";
            case pieceCode::bishop:
                return u8"\u2657";
            case pieceCode::knight:
                return u8"\u2658";
            case pieceCode::pawn:
                return u8"\u2659";
            case pieceCode::empty:
            default:
                return "";
            }
        }

        if (piece.color == playerColor::black)
        {
            switch (piece.piece)
            {
            case pieceCode::king:
                return u8"\u265A";
            case pieceCode::queen:
                return u8"\u265B";
            case pieceCode::rook:
                return u8"\u265C";
            case pieceCode::bishop:
                return u8"\u265D";
            case pieceCode::knight:
                return u8"\u265E";
            case pieceCode::pawn:
                return u8"\u265F";
            case pieceCode::empty:
            default:
                return "";
            }
        }

        return "";
    }

    std::u32string utf8_to_codepoints(const std::string &text)
    {
        std::u32string result;
        for (std::size_t i = 0; i < text.size();)
        {
            const unsigned char lead = static_cast<unsigned char>(text[i]);
            std::uint32_t codepoint = 0;
            std::size_t length = 0;

            if ((lead & 0x80U) == 0)
            {
                codepoint = lead;
                length = 1;
            }
            else if ((lead & 0xE0U) == 0xC0U && i + 1 < text.size())
            {
                codepoint = ((lead & 0x1FU) << 6) |
                            (static_cast<unsigned char>(text[i + 1]) & 0x3FU);
                length = 2;
            }
            else if ((lead & 0xF0U) == 0xE0U && i + 2 < text.size())
            {
                codepoint = ((lead & 0x0FU) << 12) |
                            ((static_cast<unsigned char>(text[i + 1]) & 0x3FU) << 6) |
                            (static_cast<unsigned char>(text[i + 2]) & 0x3FU);
                length = 3;
            }
            else if ((lead & 0xF8U) == 0xF0U && i + 3 < text.size())
            {
                codepoint = ((lead & 0x07U) << 18) |
                            ((static_cast<unsigned char>(text[i + 1]) & 0x3FU) << 12) |
                            ((static_cast<unsigned char>(text[i + 2]) & 0x3FU) << 6) |
                            (static_cast<unsigned char>(text[i + 3]) & 0x3FU);
                length = 4;
            }
            else
            {
                codepoint = '?';
                length = 1;
            }

            result.push_back(static_cast<char32_t>(codepoint));
            i += length;
        }

        return result;
    }

    bool locate_chess_font_file(std::string &font_path, std::string &error_message)
    {
        if (FcInit() == 0)
        {
            error_message = "Fontconfig initialization failed.";
            return false;
        }

        FcPattern *pattern = FcPatternCreate();
        FcCharSet *charset = FcCharSetCreate();
        if (pattern == nullptr || charset == nullptr)
        {
            if (pattern != nullptr)
                FcPatternDestroy(pattern);
            if (charset != nullptr)
                FcCharSetDestroy(charset);
            error_message = "Could not allocate Fontconfig objects.";
            return false;
        }

        for (FcChar32 codepoint = 0x2654; codepoint <= 0x265F; ++codepoint)
        {
            FcCharSetAddChar(charset, codepoint);
        }

        FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>("sans"));
        FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);
        FcPatternAddCharSet(pattern, FC_CHARSET, charset);
        FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);

        FcResult result = FcResultNoMatch;
        FcPattern *match = FcFontMatch(nullptr, pattern, &result);
        FcPatternDestroy(pattern);
        FcCharSetDestroy(charset);

        if (match == nullptr)
        {
            error_message = "Could not find a system font with Unicode chess pieces.";
            return false;
        }

        FcChar8 *file = nullptr;
        const FcResult file_result = FcPatternGetString(match, FC_FILE, 0, &file);
        if (file_result != FcResultMatch || file == nullptr)
        {
            FcPatternDestroy(match);
            error_message = "Fontconfig returned a font without a file path.";
            return false;
        }

        font_path = reinterpret_cast<const char *>(file);
        FcPatternDestroy(match);
        return true;
    }

    class FontRenderer
    {
    public:
        explicit FontRenderer(SDL_Renderer *renderer)
            : renderer_(renderer)
        {
        }

        ~FontRenderer()
        {
            for (auto &entry : glyph_cache_)
            {
                if (entry.second.texture != nullptr)
                {
                    SDL_DestroyTexture(entry.second.texture);
                }
            }

            if (face_ != nullptr)
            {
                FT_Done_Face(face_);
            }
            if (library_ != nullptr)
            {
                FT_Done_FreeType(library_);
            }
        }

        bool initialize(std::string &error_message)
        {
            std::string font_path;
            if (!locate_chess_font_file(font_path, error_message))
            {
                return false;
            }

            if (FT_Init_FreeType(&library_) != 0)
            {
                error_message = "FreeType initialization failed.";
                return false;
            }

            if (FT_New_Face(library_, font_path.c_str(), 0, &face_) != 0)
            {
                error_message = "Could not open GUI font: " + font_path;
                return false;
            }

            return true;
        }

        TextMetrics measure_text(const std::string &text, int pixel_size)
        {
            TextMetrics metrics;
            const std::u32string codepoints = utf8_to_codepoints(text);
            for (char32_t codepoint : codepoints)
            {
                const GlyphTexture &glyph = get_glyph(static_cast<std::uint32_t>(codepoint), pixel_size);
                metrics.width += glyph.advance;
                metrics.ascent = std::max(metrics.ascent, glyph.top);
                metrics.descent = std::max(metrics.descent, glyph.height - glyph.top);
            }
            metrics.height = metrics.ascent + metrics.descent;
            return metrics;
        }

        void draw_text(const std::string &text, int x, int y, int pixel_size, SDL_Color color, Uint8 alpha = 255)
        {
            const TextMetrics metrics = measure_text(text, pixel_size);
            const int baseline = y + metrics.ascent;
            int pen_x = x;

            const std::u32string codepoints = utf8_to_codepoints(text);
            for (char32_t codepoint : codepoints)
            {
                const GlyphTexture &glyph = get_glyph(static_cast<std::uint32_t>(codepoint), pixel_size);
                if (glyph.texture != nullptr)
                {
                    SDL_SetTextureColorMod(glyph.texture, color.r, color.g, color.b);
                    SDL_SetTextureAlphaMod(glyph.texture, static_cast<Uint8>((static_cast<int>(alpha) * color.a) / 255));
                    SDL_Rect dst{
                        pen_x + glyph.left,
                        baseline - glyph.top,
                        glyph.width,
                        glyph.height};
                    SDL_RenderCopy(renderer_, glyph.texture, nullptr, &dst);
                }
                pen_x += glyph.advance;
            }
        }

    private:
        const GlyphTexture &get_glyph(std::uint32_t codepoint, int pixel_size)
        {
            const GlyphKey key{codepoint, pixel_size};
            const auto found = glyph_cache_.find(key);
            if (found != glyph_cache_.end())
            {
                return found->second;
            }

            GlyphTexture glyph;
            if (FT_Set_Pixel_Sizes(face_, 0, static_cast<FT_UInt>(pixel_size)) != 0)
            {
                return glyph_cache_.emplace(key, glyph).first->second;
            }

            FT_ULong glyph_codepoint = static_cast<FT_ULong>(codepoint);
            if (FT_Load_Char(face_, glyph_codepoint, FT_LOAD_RENDER) != 0)
            {
                if (FT_Load_Char(face_, static_cast<FT_ULong>('?'), FT_LOAD_RENDER) != 0)
                {
                    return glyph_cache_.emplace(key, glyph).first->second;
                }
            }

            FT_GlyphSlot slot = face_->glyph;
            glyph.width = static_cast<int>(slot->bitmap.width);
            glyph.height = static_cast<int>(slot->bitmap.rows);
            glyph.left = slot->bitmap_left;
            glyph.top = slot->bitmap_top;
            glyph.advance = static_cast<int>(slot->advance.x >> 6);

            if (glyph.width > 0 && glyph.height > 0)
            {
                SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, glyph.width, glyph.height, 32, SDL_PIXELFORMAT_RGBA8888);
                if (surface != nullptr)
                {
                    SDL_LockSurface(surface);
                    auto *pixels = static_cast<Uint32 *>(surface->pixels);
                    for (int row = 0; row < glyph.height; ++row)
                    {
                        for (int col = 0; col < glyph.width; ++col)
                        {
                            const unsigned char coverage = slot->bitmap.buffer[(row * slot->bitmap.pitch) + col];
                            pixels[(row * surface->w) + col] = SDL_MapRGBA(surface->format, 255, 255, 255, coverage);
                        }
                    }
                    SDL_UnlockSurface(surface);

                    glyph.texture = SDL_CreateTextureFromSurface(renderer_, surface);
                    if (glyph.texture != nullptr)
                    {
                        SDL_SetTextureBlendMode(glyph.texture, SDL_BLENDMODE_BLEND);
                    }
                    SDL_FreeSurface(surface);
                }
            }

            return glyph_cache_.emplace(key, glyph).first->second;
        }

        SDL_Renderer *renderer_ = nullptr;
        FT_Library library_ = nullptr;
        FT_Face face_ = nullptr;
        std::map<GlyphKey, GlyphTexture> glyph_cache_;
    };

    TextMetrics measure_centered(FontRenderer &font_renderer, const std::string &text, int pixel_size)
    {
        return font_renderer.measure_text(text, pixel_size);
    }

    void draw_text_centered(FontRenderer &font_renderer, const std::string &text, const SDL_Rect &rect, int pixel_size, SDL_Color color)
    {
        const TextMetrics metrics = measure_centered(font_renderer, text, pixel_size);
        const int x = rect.x + std::max(0, (rect.w - metrics.width) / 2);
        const int y = rect.y + std::max(0, (rect.h - metrics.height) / 2);
        font_renderer.draw_text(text, x, y, pixel_size, color);
    }

    void draw_text_with_outline(FontRenderer &font_renderer,
                                const std::string &text,
                                int x,
                                int y,
                                int pixel_size,
                                SDL_Color fill_color,
                                SDL_Color outline_color,
                                int outline_pixels,
                                Uint8 alpha = 255)
    {
        for (int dy = -outline_pixels; dy <= outline_pixels; ++dy)
        {
            for (int dx = -outline_pixels; dx <= outline_pixels; ++dx)
            {
                if (dx == 0 && dy == 0)
                {
                    continue;
                }
                font_renderer.draw_text(text, x + dx, y + dy, pixel_size, outline_color, alpha);
            }
        }
        font_renderer.draw_text(text, x, y, pixel_size, fill_color, alpha);
    }

    std::string build_window_title(const GuiSnapshot &snapshot, ChessGuiMode mode)
    {
        std::ostringstream oss;
        std::string turn = playerColorToString(snapshot.current_player);
        if (!turn.empty())
        {
            turn[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(turn[0])));
        }
        oss << "MATE GUI | " << turn << " to move | Moves: " << snapshot.move_count;

        if (snapshot.white_checkmate)
        {
            oss << " | Checkmate: Black wins";
        }
        else if (snapshot.black_checkmate)
        {
            oss << " | Checkmate: White wins";
        }
        else if (snapshot.white_checked)
        {
            oss << " | White in check";
        }
        else if (snapshot.black_checked)
        {
            oss << " | Black in check";
        }

        if (snapshot.has_last_move)
        {
            oss << " | Last: " << square_name(snapshot.last_move_start) << " -> " << square_name(snapshot.last_move_dest);
        }

        if (mode == ChessGuiMode::busy)
        {
            oss << " | Terminal flow active";
        }

        return oss.str();
    }

    std::string status_line(const GuiSnapshot &snapshot)
    {
        if (snapshot.white_checkmate)
        {
            return "Checkmate: Black wins";
        }
        if (snapshot.black_checkmate)
        {
            return "Checkmate: White wins";
        }
        if (snapshot.white_checked)
        {
            return "White is in check";
        }
        if (snapshot.black_checked)
        {
            return "Black is in check";
        }
        return "Board is stable";
    }

    bool same_piece(pieceType left, pieceType right)
    {
        return left.piece == right.piece && left.color == right.color;
    }

    bool game_button_enabled(ChessGuiMode mode, ChessGuiActionType action)
    {
        if (mode == ChessGuiMode::local_game)
        {
            return true;
        }

        if (mode != ChessGuiMode::network_game)
        {
            return false;
        }

        switch (action)
        {
        case ChessGuiActionType::list_moves:
        case ChessGuiActionType::show_history:
        case ChessGuiActionType::write_db:
        case ChessGuiActionType::quit_game:
            return true;
        default:
            return false;
        }
    }

    std::string trimmed_copy(const std::string &text)
    {
        const auto is_ws = [](unsigned char ch)
        {
            return std::isspace(ch) != 0;
        };

        std::size_t begin = 0;
        while (begin < text.size() && is_ws(static_cast<unsigned char>(text[begin])))
        {
            ++begin;
        }

        std::size_t end = text.size();
        while (end > begin && is_ws(static_cast<unsigned char>(text[end - 1])))
        {
            --end;
        }

        return text.substr(begin, end - begin);
    }

    void pop_utf8_character(std::string &text)
    {
        if (text.empty())
        {
            return;
        }

        std::size_t index = text.size() - 1;
        while (index > 0 && (static_cast<unsigned char>(text[index]) & 0xC0U) == 0x80U)
        {
            --index;
        }
        text.erase(index);
    }

    std::string masked_password(const std::string &password)
    {
        return std::string(password.size(), '*');
    }

    std::string color_name(playerColor color)
    {
        switch (color)
        {
        case playerColor::white:
            return "White";
        case playerColor::black:
            return "Black";
        default:
            return "None";
        }
    }

    std::string editor_piece_label(pieceType piece)
    {
        if (piece.piece == pieceCode::empty)
        {
            return "Eraser";
        }

        return color_name(piece.color) + " " + pieceCodeToString(piece.piece);
    }

    std::array<SDL_Rect, kEditorPalette.size()> compute_editor_palette_rects(const Layout &layout)
    {
        std::array<SDL_Rect, kEditorPalette.size()> rects{};
        const int gap = 8;
        const int columns = 3;
        const int cell_width = std::max(58, (layout.panel_rect.w - (gap * (columns - 1))) / columns);
        const int cell_height = 58;
        const int start_y = layout.info_rect.y + layout.info_rect.h + 12;

        for (std::size_t i = 0; i < rects.size(); ++i)
        {
            const int row = static_cast<int>(i) / columns;
            const int column = static_cast<int>(i) % columns;
            rects[i] = SDL_Rect{
                layout.panel_rect.x + (column * (cell_width + gap)),
                start_y + (row * (cell_height + gap)),
                cell_width,
                cell_height};
        }

        return rects;
    }

    std::array<SDL_Rect, kEditorButtons.size()> compute_editor_button_rects(const Layout &layout)
    {
        std::array<SDL_Rect, kEditorButtons.size()> rects{};
        const int gap = 10;
        const int columns = 2;
        const int width = (layout.panel_rect.w - gap) / columns;
        const int height = 42;
        const int start_y = layout.footer_rect.y - ((height * 2) + gap) - 12;

        for (std::size_t i = 0; i < rects.size(); ++i)
        {
            const int row = static_cast<int>(i) / columns;
            const int column = static_cast<int>(i) % columns;
            rects[i] = SDL_Rect{
                layout.panel_rect.x + (column * (width + gap)),
                start_y + (row * (height + gap)),
                width,
                height};
        }

        return rects;
    }

    SDL_Rect compute_editor_save_field_rect(const Layout &layout)
    {
        return SDL_Rect{
            layout.info_rect.x,
            layout.info_rect.y + layout.info_rect.h - 38,
            layout.info_rect.w,
            34};
    }

    std::array<SDL_Rect, kDatabaseButtons.size()> compute_database_button_rects(const Layout &layout)
    {
        std::array<SDL_Rect, kDatabaseButtons.size()> rects{};
        const int gap = 10;
        const int columns = 2;
        const int width = (layout.panel_rect.w - gap) / columns;
        const int height = 46;
        const int start_y = layout.info_rect.y + layout.info_rect.h + 20;

        for (std::size_t i = 0; i < rects.size(); ++i)
        {
            const int row = static_cast<int>(i) / columns;
            const int column = static_cast<int>(i) % columns;
            rects[i] = SDL_Rect{
                layout.panel_rect.x + (column * (width + gap)),
                start_y + (row * (height + gap)),
                width,
                height};
        }

        return rects;
    }

    std::array<SDL_Rect, 2> compute_network_role_rects(const Layout &layout)
    {
        std::array<SDL_Rect, 2> rects{};
        const int gap = 10;
        const int width = (layout.panel_rect.w - gap) / 2;
        const int height = 36;
        const int y = layout.info_rect.y + 44;
        rects[0] = SDL_Rect{layout.panel_rect.x, y, width, height};
        rects[1] = SDL_Rect{layout.panel_rect.x + width + gap, y, width, height};
        return rects;
    }

    std::array<SDL_Rect, 2> compute_network_color_rects(const Layout &layout)
    {
        std::array<SDL_Rect, 2> rects{};
        const int gap = 10;
        const int width = (layout.panel_rect.w - gap) / 2;
        const int height = 36;
        const int y = layout.info_rect.y + 220;
        rects[0] = SDL_Rect{layout.panel_rect.x, y, width, height};
        rects[1] = SDL_Rect{layout.panel_rect.x + width + gap, y, width, height};
        return rects;
    }

    SDL_Rect compute_network_username_rect(const Layout &layout)
    {
        return SDL_Rect{layout.panel_rect.x, layout.info_rect.y + 94, layout.panel_rect.w, 38};
    }

    SDL_Rect compute_network_host_rect(const Layout &layout)
    {
        return SDL_Rect{layout.panel_rect.x, layout.info_rect.y + 148, layout.panel_rect.w, 38};
    }

    SDL_Rect compute_network_password_rect(const Layout &layout)
    {
        return SDL_Rect{layout.panel_rect.x, layout.info_rect.y + 274, layout.panel_rect.w, 38};
    }

    std::array<SDL_Rect, kNetworkButtons.size()> compute_network_button_rects(const Layout &layout)
    {
        std::array<SDL_Rect, kNetworkButtons.size()> rects{};
        const int gap = 10;
        const int width = (layout.panel_rect.w - gap) / 2;
        const int height = 42;
        const int y = layout.footer_rect.y - height - 8;
        rects[0] = SDL_Rect{layout.panel_rect.x, y, width, height};
        rects[1] = SDL_Rect{layout.panel_rect.x + width + gap, y, width, height};
        return rects;
    }

    SDL_Rect inset_rect(SDL_Rect rect, int amount)
    {
        rect.x += amount;
        rect.y += amount;
        rect.w -= amount * 2;
        rect.h -= amount * 2;
        return rect;
    }

    Layout compute_layout(int width, int height)
    {
        Layout layout;
        const int padding = std::max(18, std::min(width, height) / 28);
        const int menu_gap = 8;
        const int menu_height = 42;
        layout.menu_bar_rect = SDL_Rect{
            padding,
            padding,
            std::max(260, width - (2 * padding)),
            menu_height};
        const int menu_item_width = std::max(90, (layout.menu_bar_rect.w - (menu_gap * static_cast<int>(kMenuItems.size() - 1))) / static_cast<int>(kMenuItems.size()));
        int menu_x = layout.menu_bar_rect.x;
        for (std::size_t i = 0; i < kMenuItems.size(); ++i)
        {
            layout.menu_item_rects[i] = SDL_Rect{
                menu_x,
                layout.menu_bar_rect.y,
                menu_item_width,
                menu_height};
            menu_x += menu_item_width + menu_gap;
        }

        const int min_board_size = 8 * 42;
        const int content_top = layout.menu_bar_rect.y + layout.menu_bar_rect.h + padding;
        int panel_width = std::clamp(width / 4, 210, 270);
        int board_size = std::min(height - content_top - padding, width - panel_width - (3 * padding));
        board_size = std::max(min_board_size, (board_size / 8) * 8);

        if (board_size + panel_width + (3 * padding) > width)
        {
            panel_width = std::max(180, width - board_size - (3 * padding));
        }

        board_size = std::min(board_size, height - content_top - padding);
        board_size = (board_size / 8) * 8;

        layout.square_size = board_size / 8;
        const int content_height = std::max(board_size, height - content_top - padding);
        layout.board_rect = SDL_Rect{padding, content_top + ((content_height - board_size) / 2), board_size, board_size};
        layout.panel_rect = SDL_Rect{
            layout.board_rect.x + layout.board_rect.w + padding,
            content_top,
            std::max(180, width - layout.board_rect.x - layout.board_rect.w - (2 * padding)),
            std::max(160, height - content_top - padding)};

        const int info_height = std::clamp(layout.panel_rect.h / 4, 110, 150);
        layout.info_rect = SDL_Rect{
            layout.panel_rect.x,
            layout.panel_rect.y,
            layout.panel_rect.w,
            info_height};

        const int footer_height = 54;
        const int button_gap = 10;
        const int available_buttons_height = layout.panel_rect.h - layout.info_rect.h - footer_height - ((static_cast<int>(kButtons.size()) - 1) * button_gap);
        const int button_height = std::clamp(available_buttons_height / static_cast<int>(kButtons.size()), 38, 52);
        int button_y = layout.info_rect.y + layout.info_rect.h + 12;
        for (std::size_t i = 0; i < kButtons.size(); ++i)
        {
            layout.button_rects[i] = SDL_Rect{
                layout.panel_rect.x,
                button_y,
                layout.panel_rect.w,
                button_height};
            button_y += button_height + button_gap;
        }

        layout.footer_rect = SDL_Rect{
            layout.panel_rect.x,
            layout.panel_rect.y + layout.panel_rect.h - footer_height,
            layout.panel_rect.w,
            footer_height};

        return layout;
    }

    int button_index_at(const Layout &layout, int x, int y)
    {
        for (std::size_t i = 0; i < layout.button_rects.size(); ++i)
        {
            if (point_in_rect(x, y, layout.button_rects[i]))
            {
                return static_cast<int>(i);
            }
        }

        return -1;
    }

    int menu_index_at(const Layout &layout, int x, int y)
    {
        for (std::size_t i = 0; i < layout.menu_item_rects.size(); ++i)
        {
            if (point_in_rect(x, y, layout.menu_item_rects[i]))
            {
                return static_cast<int>(i);
            }
        }

        return -1;
    }

    bool point_to_square(const Layout &layout, int x, int y, boardCoordinateType &square)
    {
        if (!point_in_rect(x, y, layout.board_rect))
        {
            return false;
        }

        const int file = (x - layout.board_rect.x) / layout.square_size;
        const int rank_from_top = (y - layout.board_rect.y) / layout.square_size;
        square.file = static_cast<char>('A' + file);
        square.rank = 8 - rank_from_top;
        return true;
    }

    SDL_Rect square_to_rect(const Layout &layout, boardCoordinateType square)
    {
        const int file = square.file - 'A';
        const int rank_from_top = 8 - square.rank;
        return SDL_Rect{
            layout.board_rect.x + (file * layout.square_size),
            layout.board_rect.y + (rank_from_top * layout.square_size),
            layout.square_size,
            layout.square_size};
    }

    bool find_king_square(const GuiSnapshot &snapshot, playerColor color, boardCoordinateType &out_square)
    {
        for (int file = 0; file < 8; ++file)
        {
            for (int rank = 0; rank < 8; ++rank)
            {
                const pieceType &piece = snapshot.board[static_cast<std::size_t>(file)][static_cast<std::size_t>(rank)];
                if (piece.piece == pieceCode::king && piece.color == color)
                {
                    out_square = {static_cast<char>('A' + file), rank + 1};
                    return true;
                }
            }
        }
        return false;
    }

    class SdlChessGui final : public ChessGui
    {
    public:
        SdlChessGui()
            : snapshot_{make_empty_board()}
        {
            worker_ = std::thread(&SdlChessGui::thread_main, this);
        }

        ~SdlChessGui() override
        {
            running_.store(false);
            if (worker_.joinable())
            {
                worker_.join();
            }
        }

        bool wait_until_initialized(std::string &error_message)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            init_cv_.wait(lock, [this]()
                          { return initialized_; });

            if (!init_success_)
            {
                error_message = init_error_;
                return false;
            }

            return true;
        }

        void sync(const chess &game) override
        {
            GuiSnapshot next{make_empty_board()};
            next.board = game.board();
            next.current_player = game.current_player_color();
            next.white_checked = game.is_checked(playerColor::white);
            next.black_checked = game.is_checked(playerColor::black);
            next.white_checkmate = game.is_checkmate(playerColor::white);
            next.black_checkmate = game.is_checkmate(playerColor::black);
            next.move_count = game.move_count();
            next.has_last_move = game.has_played_moves();

            if (next.has_last_move)
            {
                const motionType last_move = game.getHistoryLast();
                next.last_move_start = last_move.start_position.coord;
                next.last_move_dest = last_move.dest_position.coord;
            }

            std::lock_guard<std::mutex> lock(mutex_);
            snapshot_ = next;
            title_dirty_ = true;
        }

        bool is_open() const override
        {
            return open_.load();
        }

        void set_mode(ChessGuiMode mode) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            mode_ = mode;
            title_dirty_ = true;
            if (mode_ == ChessGuiMode::busy)
            {
                pending_actions_.clear();
            }
            dragging_ = false;
            pressed_button_index_ = -1;
            hovered_button_index_ = -1;
            pressed_menu_index_ = -1;
            hovered_menu_index_ = -1;
            active_text_field_ = TextInputField::none;
        }

        void set_board_editor_state(const ChessGuiBoardEditorState &state) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            board_editor_state_ = state;
            title_dirty_ = true;
        }

        ChessGuiBoardEditorState board_editor_state() const override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return board_editor_state_;
        }

        void set_database_state(const ChessGuiDatabaseState &state) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            database_state_ = state;
            title_dirty_ = true;
        }

        ChessGuiDatabaseState database_state() const override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return database_state_;
        }

        void set_network_state(const ChessGuiNetworkState &state) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            network_state_ = state;
            title_dirty_ = true;
        }

        ChessGuiNetworkState network_state() const override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return network_state_;
        }

        bool poll_action(ChessGuiAction &action) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_actions_.empty())
            {
                return false;
            }

            action = pending_actions_.front();
            pending_actions_.pop_front();
            return true;
        }

    private:
        void mark_initialized(bool success, const std::string &error_message)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                initialized_ = true;
                init_success_ = success;
                init_error_ = error_message;
            }
            init_cv_.notify_all();
        }

        void thread_main()
        {
            if (SDL_Init(SDL_INIT_VIDEO) != 0)
            {
                mark_initialized(false, SDL_GetError());
                return;
            }

            SDL_Window *window = SDL_CreateWindow(
                "MATE GUI",
                SDL_WINDOWPOS_CENTERED,
                SDL_WINDOWPOS_CENTERED,
                kInitialWindowWidth,
                kInitialWindowHeight,
                SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

            if (window == nullptr)
            {
                const std::string error_message = SDL_GetError();
                SDL_Quit();
                mark_initialized(false, error_message);
                return;
            }
            SDL_SetWindowMinimumSize(window, 760, 560);

            SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (renderer == nullptr)
            {
                renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
            }
            if (renderer == nullptr)
            {
                const std::string error_message = SDL_GetError();
                SDL_DestroyWindow(window);
                SDL_Quit();
                mark_initialized(false, error_message);
                return;
            }

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_StartTextInput();

            std::unique_ptr<FontRenderer> font_renderer(new FontRenderer(renderer));
            std::string font_error;
            if (!font_renderer->initialize(font_error))
            {
                font_renderer.reset();
                SDL_DestroyRenderer(renderer);
                SDL_DestroyWindow(window);
                SDL_Quit();
                mark_initialized(false, font_error);
                return;
            }

            open_.store(true);
            mark_initialized(true, "");

            while (running_.load())
            {
                SDL_Event event;
                while (SDL_PollEvent(&event) == 1)
                {
                    if (event.type == SDL_QUIT)
                    {
                        running_.store(false);
                        open_.store(false);
                    }
                    else
                    {
                        handle_event(event, window);
                    }
                }

                GuiSnapshot snapshot_copy{make_empty_board()};
                ChessGuiMode mode_copy = ChessGuiMode::main_menu;
                ChessGuiBoardEditorState editor_state_copy;
                ChessGuiDatabaseState database_state_copy;
                ChessGuiNetworkState network_state_copy;
                TextInputField active_text_field = TextInputField::none;
                bool dragging_copy = false;
                boardCoordinateType drag_from_copy{'A', 1};
                pieceType drag_piece_copy{pieceCode::empty, playerColor::none};
                int drag_mouse_x = 0;
                int drag_mouse_y = 0;
                int hovered_menu = -1;
                int pressed_menu = -1;
                int hovered_button = -1;
                int pressed_button = -1;
                bool title_dirty = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    snapshot_copy = snapshot_;
                    mode_copy = mode_;
                    editor_state_copy = board_editor_state_;
                    database_state_copy = database_state_;
                    network_state_copy = network_state_;
                    active_text_field = active_text_field_;
                    dragging_copy = dragging_;
                    drag_from_copy = drag_from_;
                    drag_piece_copy = drag_piece_;
                    drag_mouse_x = drag_mouse_x_;
                    drag_mouse_y = drag_mouse_y_;
                    hovered_menu = hovered_menu_index_;
                    pressed_menu = pressed_menu_index_;
                    hovered_button = hovered_button_index_;
                    pressed_button = pressed_button_index_;
                    title_dirty = title_dirty_;
                    title_dirty_ = false;
                }

                if (title_dirty)
                {
                    const std::string title = build_window_title(snapshot_copy, mode_copy);
                    SDL_SetWindowTitle(window, title.c_str());
                }

                render_snapshot(renderer,
                                *font_renderer,
                                snapshot_copy,
                                mode_copy,
                                editor_state_copy,
                                database_state_copy,
                                network_state_copy,
                                active_text_field,
                                dragging_copy,
                                drag_from_copy,
                                drag_piece_copy,
                                drag_mouse_x,
                                drag_mouse_y,
                                hovered_menu,
                                pressed_menu,
                                hovered_button,
                                pressed_button);
                SDL_Delay(16);
            }

            font_renderer.reset();
            SDL_StopTextInput();
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            open_.store(false);
        }

        std::string *active_text_field_ptr_locked()
        {
            switch (active_text_field_)
            {
            case TextInputField::editor_save_name:
                return &board_editor_state_.save_name;
            case TextInputField::network_username:
                return &network_state_.username;
            case TextInputField::network_host:
                return &network_state_.host;
            case TextInputField::network_password:
                return &network_state_.password;
            case TextInputField::none:
            default:
                return nullptr;
            }
        }

        void cycle_text_field_locked()
        {
            if (mode_ == ChessGuiMode::board_editor)
            {
                active_text_field_ = TextInputField::editor_save_name;
                return;
            }

            if (mode_ == ChessGuiMode::network_setup)
            {
                switch (active_text_field_)
                {
                case TextInputField::network_username:
                    active_text_field_ = (network_state_.role == ChessGuiNetworkRole::join) ? TextInputField::network_host : TextInputField::network_password;
                    break;
                case TextInputField::network_host:
                    active_text_field_ = TextInputField::network_password;
                    break;
                case TextInputField::network_password:
                case TextInputField::none:
                default:
                    active_text_field_ = TextInputField::network_username;
                    break;
                }
            }
        }

        void handle_event(const SDL_Event &event, SDL_Window *window)
        {
            int width = 0;
            int height = 0;
            SDL_GetWindowSize(window, &width, &height);
            const Layout layout = compute_layout(width, height);

            std::lock_guard<std::mutex> lock(mutex_);

            if (event.type == SDL_TEXTINPUT)
            {
                std::string *field = active_text_field_ptr_locked();
                if (field != nullptr && mode_ != ChessGuiMode::busy)
                {
                    field->append(event.text.text);
                }
                return;
            }

            if (event.type == SDL_KEYDOWN)
            {
                if (event.key.keysym.sym == SDLK_BACKSPACE)
                {
                    std::string *field = active_text_field_ptr_locked();
                    if (field != nullptr)
                    {
                        pop_utf8_character(*field);
                    }
                }
                else if (event.key.keysym.sym == SDLK_TAB)
                {
                    cycle_text_field_locked();
                }
                return;
            }

            if (event.type == SDL_MOUSEMOTION)
            {
                drag_mouse_x_ = event.motion.x;
                drag_mouse_y_ = event.motion.y;
                hovered_menu_index_ = menu_index_at(layout, event.motion.x, event.motion.y);
                hovered_button_index_ = button_index_at(layout, event.motion.x, event.motion.y);
                return;
            }

            if (event.type != SDL_MOUSEBUTTONDOWN && event.type != SDL_MOUSEBUTTONUP)
            {
                return;
            }

            const int mouse_x = event.button.x;
            const int mouse_y = event.button.y;
            drag_mouse_x_ = mouse_x;
            drag_mouse_y_ = mouse_y;
            hovered_menu_index_ = menu_index_at(layout, mouse_x, mouse_y);
            hovered_button_index_ = button_index_at(layout, mouse_x, mouse_y);

            if (event.button.button != SDL_BUTTON_LEFT)
            {
                return;
            }

            if (mode_ == ChessGuiMode::busy)
            {
                if (event.type == SDL_MOUSEBUTTONUP)
                {
                    pressed_menu_index_ = -1;
                    pressed_button_index_ = -1;
                    dragging_ = false;
                }
                return;
            }

            if (event.type == SDL_MOUSEBUTTONDOWN)
            {
                const int menu_index = menu_index_at(layout, mouse_x, mouse_y);
                if (mode_ == ChessGuiMode::main_menu && menu_index >= 0)
                {
                    pressed_menu_index_ = menu_index;
                    return;
                }

                const int button_index = button_index_at(layout, mouse_x, mouse_y);
                if ((mode_ == ChessGuiMode::local_game || mode_ == ChessGuiMode::network_game) &&
                    button_index >= 0 &&
                    game_button_enabled(mode_, kButtons[static_cast<std::size_t>(button_index)].action))
                {
                    pressed_button_index_ = button_index;
                    return;
                }

                if (mode_ == ChessGuiMode::board_editor)
                {
                    if (point_in_rect(mouse_x, mouse_y, compute_editor_save_field_rect(layout)))
                    {
                        active_text_field_ = TextInputField::editor_save_name;
                        return;
                    }
                }
                else if (mode_ == ChessGuiMode::network_setup)
                {
                    if (point_in_rect(mouse_x, mouse_y, compute_network_username_rect(layout)))
                    {
                        active_text_field_ = TextInputField::network_username;
                        return;
                    }
                    if (network_state_.role == ChessGuiNetworkRole::join &&
                        point_in_rect(mouse_x, mouse_y, compute_network_host_rect(layout)))
                    {
                        active_text_field_ = TextInputField::network_host;
                        return;
                    }
                    if (point_in_rect(mouse_x, mouse_y, compute_network_password_rect(layout)))
                    {
                        active_text_field_ = TextInputField::network_password;
                        return;
                    }
                }
                else if (mode_ != ChessGuiMode::local_game && mode_ != ChessGuiMode::network_game)
                {
                    return;
                }

                if (mode_ != ChessGuiMode::local_game && mode_ != ChessGuiMode::network_game)
                {
                    return;
                }

                boardCoordinateType square{'A', 1};
                if (!point_to_square(layout, mouse_x, mouse_y, square))
                {
                    return;
                }

                const pieceType &piece = snapshot_.board[static_cast<std::size_t>(square.file - 'A')][static_cast<std::size_t>(square.rank - 1)];
                if (piece.piece == pieceCode::empty || piece.color != snapshot_.current_player)
                {
                    return;
                }

                dragging_ = true;
                drag_from_ = square;
                drag_piece_ = piece;
                return;
            }

            if (pressed_menu_index_ >= 0)
            {
                const int released_on = menu_index_at(layout, mouse_x, mouse_y);
                if (mode_ == ChessGuiMode::main_menu && released_on == pressed_menu_index_)
                {
                    pending_actions_.push_back(ChessGuiAction{kMenuItems[static_cast<std::size_t>(released_on)].action, {'A', 1}, {'A', 1}});
                }
                pressed_menu_index_ = -1;
                return;
            }

            if (pressed_button_index_ >= 0)
            {
                const int released_on = button_index_at(layout, mouse_x, mouse_y);
                if (released_on == pressed_button_index_ &&
                    game_button_enabled(mode_, kButtons[static_cast<std::size_t>(released_on)].action))
                {
                    pending_actions_.push_back(ChessGuiAction{kButtons[static_cast<std::size_t>(released_on)].action, {'A', 1}, {'A', 1}});
                }
                pressed_button_index_ = -1;
                return;
            }

            if (mode_ == ChessGuiMode::board_editor)
            {
                const auto palette_rects = compute_editor_palette_rects(layout);
                for (std::size_t i = 0; i < palette_rects.size(); ++i)
                {
                    if (point_in_rect(mouse_x, mouse_y, palette_rects[i]))
                    {
                        board_editor_state_.selected_piece = kEditorPalette[i].piece;
                        return;
                    }
                }

                const auto button_rects = compute_editor_button_rects(layout);
                for (std::size_t i = 0; i < button_rects.size(); ++i)
                {
                    if (point_in_rect(mouse_x, mouse_y, button_rects[i]))
                    {
                        pending_actions_.push_back(ChessGuiAction{kEditorButtons[i].action, {'A', 1}, {'A', 1}});
                        return;
                    }
                }

                boardCoordinateType square{'A', 1};
                if (point_to_square(layout, mouse_x, mouse_y, square))
                {
                    pending_actions_.push_back(ChessGuiAction{ChessGuiActionType::editor_board_click, {'A', 1}, square});
                }
                return;
            }

            if (mode_ == ChessGuiMode::database_browser)
            {
                const auto button_rects = compute_database_button_rects(layout);
                for (std::size_t i = 0; i < button_rects.size(); ++i)
                {
                    if (!point_in_rect(mouse_x, mouse_y, button_rects[i]))
                    {
                        continue;
                    }

                    if (i == 0 && database_state_.selected_game_index > 0)
                    {
                        database_state_.selected_game_index--;
                        database_state_.selected_snapshot_index = 0;
                        pending_actions_.push_back(ChessGuiAction{ChessGuiActionType::database_selection_changed, {'A', 1}, {'A', 1}});
                    }
                    else if (i == 1 && database_state_.selected_game_index + 1 < static_cast<int>(database_state_.games.size()))
                    {
                        database_state_.selected_game_index++;
                        database_state_.selected_snapshot_index = 0;
                        pending_actions_.push_back(ChessGuiAction{ChessGuiActionType::database_selection_changed, {'A', 1}, {'A', 1}});
                    }
                    else if (i == 2 && database_state_.selected_snapshot_index > 0)
                    {
                        database_state_.selected_snapshot_index--;
                        pending_actions_.push_back(ChessGuiAction{ChessGuiActionType::database_selection_changed, {'A', 1}, {'A', 1}});
                    }
                    else if (i == 3 && database_state_.selected_snapshot_index + 1 < database_state_.snapshot_count)
                    {
                        database_state_.selected_snapshot_index++;
                        pending_actions_.push_back(ChessGuiAction{ChessGuiActionType::database_selection_changed, {'A', 1}, {'A', 1}});
                    }
                    else if (i == 4)
                    {
                        pending_actions_.push_back(ChessGuiAction{ChessGuiActionType::database_load_snapshot, {'A', 1}, {'A', 1}});
                    }
                    else if (i == 5)
                    {
                        pending_actions_.push_back(ChessGuiAction{ChessGuiActionType::database_back, {'A', 1}, {'A', 1}});
                    }
                    return;
                }
                return;
            }

            if (mode_ == ChessGuiMode::network_setup)
            {
                const auto role_rects = compute_network_role_rects(layout);
                if (point_in_rect(mouse_x, mouse_y, role_rects[0]))
                {
                    network_state_.role = ChessGuiNetworkRole::host;
                    active_text_field_ = TextInputField::network_username;
                    return;
                }
                if (point_in_rect(mouse_x, mouse_y, role_rects[1]))
                {
                    network_state_.role = ChessGuiNetworkRole::join;
                    active_text_field_ = TextInputField::network_username;
                    return;
                }

                if (network_state_.role == ChessGuiNetworkRole::host)
                {
                    const auto color_rects = compute_network_color_rects(layout);
                    if (point_in_rect(mouse_x, mouse_y, color_rects[0]))
                    {
                        network_state_.host_plays_white = true;
                        return;
                    }
                    if (point_in_rect(mouse_x, mouse_y, color_rects[1]))
                    {
                        network_state_.host_plays_white = false;
                        return;
                    }
                }

                const auto button_rects = compute_network_button_rects(layout);
                for (std::size_t i = 0; i < button_rects.size(); ++i)
                {
                    if (point_in_rect(mouse_x, mouse_y, button_rects[i]))
                    {
                        pending_actions_.push_back(ChessGuiAction{kNetworkButtons[i].action, {'A', 1}, {'A', 1}});
                        return;
                    }
                }
                return;
            }

            if (!dragging_)
            {
                return;
            }

            boardCoordinateType dest_square{'A', 1};
            if (point_to_square(layout, mouse_x, mouse_y, dest_square) && !same_square(dest_square, drag_from_))
            {
                ChessGuiAction action;
                action.type = ChessGuiActionType::move_piece;
                action.start = drag_from_;
                action.dest = dest_square;
                pending_actions_.push_back(action);
            }

            dragging_ = false;
        }

        void render_snapshot(SDL_Renderer *renderer,
                             FontRenderer &font_renderer,
                             const GuiSnapshot &snapshot,
                             ChessGuiMode mode,
                             const ChessGuiBoardEditorState &board_editor_state,
                             const ChessGuiDatabaseState &database_state,
                             const ChessGuiNetworkState &network_state,
                             TextInputField active_text_field,
                             bool dragging,
                             boardCoordinateType drag_from,
                             pieceType drag_piece,
                             int drag_mouse_x,
                             int drag_mouse_y,
                             int hovered_menu,
                             int pressed_menu,
                             int hovered_button,
                             int pressed_button)
        {
            int width = 0;
            int height = 0;
            SDL_GetRendererOutputSize(renderer, &width, &height);
            const Layout layout = compute_layout(width, height);

            const SDL_Color background = make_color(16, 24, 30);
            const SDL_Color panel_bg = make_color(29, 43, 54);
            const SDL_Color board_border = make_color(111, 143, 168);
            const SDL_Color light_square = make_color(243, 222, 189);
            const SDL_Color dark_square = make_color(165, 117, 80);
            const SDL_Color last_move_light = make_color(247, 244, 121, 180);
            const SDL_Color last_move_dark = make_color(211, 204, 76, 180);
            const SDL_Color check_outline = make_color(218, 68, 83);
            const SDL_Color white_piece_fill = make_color(251, 248, 239);
            const SDL_Color white_piece_outline = make_color(36, 43, 49);
            const SDL_Color black_piece_fill = make_color(39, 44, 51);
            const SDL_Color black_piece_outline = make_color(235, 239, 244);
            const SDL_Color label_color = make_color(225, 233, 241);
            const SDL_Color muted_label = make_color(164, 177, 188);
            const SDL_Color menu_bar_fill = make_color(25, 37, 46);
            const SDL_Color menu_item_fill = make_color(47, 73, 92);
            const SDL_Color menu_item_hover = make_color(62, 93, 116);
            const SDL_Color menu_item_pressed = make_color(37, 60, 77);
            const SDL_Color menu_item_disabled = make_color(52, 58, 64);
            const SDL_Color menu_outline = make_color(113, 144, 168);
            const SDL_Color button_fill = make_color(55, 102, 140);
            const SDL_Color button_hover = make_color(69, 123, 166);
            const SDL_Color button_pressed = make_color(44, 84, 116);
            const SDL_Color button_disabled = make_color(64, 73, 81);
            const SDL_Color button_outline = make_color(124, 154, 177);
            const SDL_Color panel_outline = make_color(77, 100, 118);

            set_draw_color(renderer, background);
            SDL_RenderClear(renderer);

            fill_rect(renderer, layout.menu_bar_rect, menu_bar_fill);
            draw_rect(renderer, layout.menu_bar_rect, menu_outline);

            const bool menu_enabled = mode == ChessGuiMode::main_menu;
            for (std::size_t i = 0; i < kMenuItems.size(); ++i)
            {
                SDL_Color fill = menu_item_fill;
                if (!menu_enabled)
                {
                    fill = menu_item_disabled;
                }
                else if (static_cast<int>(i) == pressed_menu)
                {
                    fill = menu_item_pressed;
                }
                else if (static_cast<int>(i) == hovered_menu)
                {
                    fill = menu_item_hover;
                }

                fill_rect(renderer, layout.menu_item_rects[i], fill);
                draw_rect(renderer, layout.menu_item_rects[i], menu_outline);
                draw_text_centered(font_renderer, kMenuItems[i].label, layout.menu_item_rects[i], 18, label_color);
            }

            fill_rect(renderer, layout.panel_rect, panel_bg);
            draw_rect(renderer, layout.panel_rect, panel_outline);

            SDL_Rect border_rect{
                layout.board_rect.x - 4,
                layout.board_rect.y - 4,
                layout.board_rect.w + 8,
                layout.board_rect.h + 8};
            fill_rect(renderer, border_rect, board_border);

            boardCoordinateType white_king_square{'A', 1};
            boardCoordinateType black_king_square{'A', 1};
            const bool have_white_king = find_king_square(snapshot, playerColor::white, white_king_square);
            const bool have_black_king = find_king_square(snapshot, playerColor::black, black_king_square);

            for (int display_rank = 8; display_rank >= 1; --display_rank)
            {
                for (int file = 0; file < 8; ++file)
                {
                    const boardCoordinateType current_square{static_cast<char>('A' + file), display_rank};
                    SDL_Rect square_rect = square_to_rect(layout, current_square);
                    const bool is_light_square = ((file + display_rank) % 2 == 1);
                    fill_rect(renderer, square_rect, is_light_square ? light_square : dark_square);

                    if (snapshot.has_last_move &&
                        (same_square(current_square, snapshot.last_move_start) || same_square(current_square, snapshot.last_move_dest)))
                    {
                        fill_rect(renderer, square_rect, is_light_square ? last_move_light : last_move_dark);
                    }

                    const bool white_king_checked = snapshot.white_checked && have_white_king && same_square(current_square, white_king_square);
                    const bool black_king_checked = snapshot.black_checked && have_black_king && same_square(current_square, black_king_square);
                    if (white_king_checked || black_king_checked)
                    {
                        draw_rect(renderer, square_rect, check_outline);
                        SDL_Rect inner = square_rect;
                        inner.x += 2;
                        inner.y += 2;
                        inner.w -= 4;
                        inner.h -= 4;
                        draw_rect(renderer, inner, check_outline);
                    }

                    if (dragging && same_square(current_square, drag_from))
                    {
                        continue;
                    }

                    const pieceType &piece = snapshot.board[static_cast<std::size_t>(file)][static_cast<std::size_t>(display_rank - 1)];
                    const std::string symbol = piece_symbol_utf8(piece);
                    if (symbol.empty())
                    {
                        continue;
                    }

                    const SDL_Color fill = (piece.color == playerColor::white) ? white_piece_fill : black_piece_fill;
                    const SDL_Color outline = (piece.color == playerColor::white) ? white_piece_outline : black_piece_outline;
                    const int piece_size = std::max(24, (layout.square_size * 3) / 4);
                    const TextMetrics metrics = font_renderer.measure_text(symbol, piece_size);
                    const int piece_x = square_rect.x + std::max(0, (square_rect.w - metrics.width) / 2);
                    const int piece_y = square_rect.y + std::max(0, (square_rect.h - metrics.height) / 2) - 2;
                    draw_text_with_outline(font_renderer, symbol, piece_x, piece_y, piece_size, fill, outline, 1);
                }
            }

            if (dragging)
            {
                const std::string symbol = piece_symbol_utf8(drag_piece);
                if (!symbol.empty())
                {
                    const SDL_Color fill = (drag_piece.color == playerColor::white) ? white_piece_fill : black_piece_fill;
                    const SDL_Color outline = (drag_piece.color == playerColor::white) ? white_piece_outline : black_piece_outline;
                    const int piece_size = std::max(24, (layout.square_size * 3) / 4);
                    const TextMetrics metrics = font_renderer.measure_text(symbol, piece_size);
                    const int piece_x = drag_mouse_x - (metrics.width / 2);
                    const int piece_y = drag_mouse_y - (metrics.height / 2);
                    draw_text_with_outline(font_renderer, symbol, piece_x, piece_y, piece_size, fill, outline, 1, 230);
                }
            }

            const int label_size = std::max(14, layout.square_size / 4);
            for (int file = 0; file < 8; ++file)
            {
                SDL_Rect top_rect{
                    layout.board_rect.x + (file * layout.square_size),
                    layout.board_rect.y - 22,
                    layout.square_size,
                    20};
                SDL_Rect bottom_rect{
                    layout.board_rect.x + (file * layout.square_size),
                    layout.board_rect.y + layout.board_rect.h + 4,
                    layout.square_size,
                    20};
                const std::string label(1, static_cast<char>('A' + file));
                draw_text_centered(font_renderer, label, top_rect, label_size, label_color);
                draw_text_centered(font_renderer, label, bottom_rect, label_size, label_color);
            }

            for (int rank = 8; rank >= 1; --rank)
            {
                SDL_Rect left_rect{
                    layout.board_rect.x - 22,
                    layout.board_rect.y + ((8 - rank) * layout.square_size),
                    20,
                    layout.square_size};
                SDL_Rect right_rect{
                    layout.board_rect.x + layout.board_rect.w + 4,
                    layout.board_rect.y + ((8 - rank) * layout.square_size),
                    20,
                    layout.square_size};
                const std::string label = std::to_string(rank);
                draw_text_centered(font_renderer, label, left_rect, label_size, label_color);
                draw_text_centered(font_renderer, label, right_rect, label_size, label_color);
            }

            const int title_size = 28;
            SDL_Rect title_rect{layout.info_rect.x, layout.info_rect.y, layout.info_rect.w, 34};
            draw_text_centered(font_renderer, "MATE GUI", title_rect, title_size, label_color);

            const int info_size = 18;
            if (mode == ChessGuiMode::board_editor)
            {
                font_renderer.draw_text("Board editor", layout.info_rect.x + 8, layout.info_rect.y + 42, info_size, label_color);
                font_renderer.draw_text("Selected: " + editor_piece_label(board_editor_state.selected_piece),
                                        layout.info_rect.x + 8,
                                        layout.info_rect.y + 68,
                                        16,
                                        label_color);
                font_renderer.draw_text(trimmed_copy(board_editor_state.status_message).empty() ? "Click a square to place the selected piece." : board_editor_state.status_message,
                                        layout.info_rect.x + 8,
                                        layout.info_rect.y + 92,
                                        15,
                                        muted_label);

                font_renderer.draw_text("Save name", layout.info_rect.x + 8, layout.info_rect.y + layout.info_rect.h - 58, 15, muted_label);
                const SDL_Rect save_rect = compute_editor_save_field_rect(layout);
                const SDL_Color field_fill = (active_text_field == TextInputField::editor_save_name) ? button_hover : menu_item_fill;
                fill_rect(renderer, save_rect, field_fill);
                draw_rect(renderer, save_rect, button_outline);
                font_renderer.draw_text(board_editor_state.save_name.empty() ? "Custom_Board" : board_editor_state.save_name,
                                        save_rect.x + 8,
                                        save_rect.y + 8,
                                        17,
                                        label_color);

                const auto palette_rects = compute_editor_palette_rects(layout);
                for (std::size_t i = 0; i < palette_rects.size(); ++i)
                {
                    SDL_Color fill = same_piece(board_editor_state.selected_piece, kEditorPalette[i].piece) ? button_hover : menu_item_fill;
                    fill_rect(renderer, palette_rects[i], fill);
                    draw_rect(renderer, palette_rects[i], button_outline);

                    const std::string symbol = piece_symbol_utf8(kEditorPalette[i].piece);
                    if (!symbol.empty())
                    {
                        const SDL_Color fill_color = (kEditorPalette[i].piece.color == playerColor::white) ? white_piece_fill : black_piece_fill;
                        const SDL_Color outline_color = (kEditorPalette[i].piece.color == playerColor::white) ? white_piece_outline : black_piece_outline;
                        draw_text_with_outline(font_renderer,
                                               symbol,
                                               palette_rects[i].x + std::max(0, (palette_rects[i].w - font_renderer.measure_text(symbol, 28).width) / 2),
                                               palette_rects[i].y + 8,
                                               28,
                                               fill_color,
                                               outline_color,
                                               1);
                    }
                    else
                    {
                        draw_text_centered(font_renderer, "X", inset_rect(palette_rects[i], 6), 22, label_color);
                    }

                    font_renderer.draw_text(kEditorPalette[i].label,
                                            palette_rects[i].x + 6,
                                            palette_rects[i].y + palette_rects[i].h - 18,
                                            11,
                                            muted_label);
                }

                const auto button_rects = compute_editor_button_rects(layout);
                for (std::size_t i = 0; i < button_rects.size(); ++i)
                {
                    fill_rect(renderer, button_rects[i], button_fill);
                    draw_rect(renderer, button_rects[i], button_outline);
                    draw_text_centered(font_renderer, kEditorButtons[i].label, button_rects[i], 18, label_color);
                }

                font_renderer.draw_text("Use Save to write the current board to the database.", layout.footer_rect.x + 8, layout.footer_rect.y + 10, 14, muted_label);
                font_renderer.draw_text("Tab switches fields. Back returns to the main menu.", layout.footer_rect.x + 8, layout.footer_rect.y + 28, 14, muted_label);
            }
            else if (mode == ChessGuiMode::database_browser)
            {
                font_renderer.draw_text("Load from database", layout.info_rect.x + 8, layout.info_rect.y + 42, info_size, label_color);
                if (database_state.games.empty() || database_state.selected_game_index < 0)
                {
                    font_renderer.draw_text(database_state.status_message.empty() ? "No saved games found." : database_state.status_message,
                                            layout.info_rect.x + 8,
                                            layout.info_rect.y + 72,
                                            16,
                                            muted_label);
                }
                else
                {
                    const ChessGuiDatabaseEntry &game_entry = database_state.games[static_cast<std::size_t>(database_state.selected_game_index)];
                    font_renderer.draw_text(game_entry.name, layout.info_rect.x + 8, layout.info_rect.y + 68, 18, label_color);
                    font_renderer.draw_text("Game " + std::to_string(database_state.selected_game_index + 1) + " of " + std::to_string(database_state.games.size()) +
                                                " | Snapshots: " + std::to_string(std::max(0, database_state.snapshot_count)),
                                            layout.info_rect.x + 8,
                                            layout.info_rect.y + 92,
                                            15,
                                            muted_label);
                    font_renderer.draw_text("Board " + std::to_string(database_state.selected_snapshot_index + 1) + " of " + std::to_string(std::max(1, database_state.snapshot_count)),
                                            layout.info_rect.x + 8,
                                            layout.info_rect.y + 114,
                                            15,
                                            muted_label);
                    if (!database_state.current_move_label.empty())
                    {
                        font_renderer.draw_text(database_state.current_move_label, layout.info_rect.x + 8, layout.info_rect.y + 136, 13, muted_label);
                    }
                }

                if (!database_state.status_message.empty())
                {
                    font_renderer.draw_text(database_state.status_message, layout.panel_rect.x, layout.panel_rect.y + layout.panel_rect.h - 84, 14, muted_label);
                }

                const auto button_rects = compute_database_button_rects(layout);
                for (std::size_t i = 0; i < button_rects.size(); ++i)
                {
                    bool enabled = true;
                    if (i == 0)
                    {
                        enabled = database_state.selected_game_index > 0;
                    }
                    else if (i == 1)
                    {
                        enabled = database_state.selected_game_index + 1 < static_cast<int>(database_state.games.size());
                    }
                    else if (i == 2)
                    {
                        enabled = database_state.selected_snapshot_index > 0;
                    }
                    else if (i == 3)
                    {
                        enabled = database_state.selected_snapshot_index + 1 < database_state.snapshot_count;
                    }
                    else if (i == 4)
                    {
                        enabled = database_state.snapshot_count > 0;
                    }

                    fill_rect(renderer, button_rects[i], enabled ? button_fill : button_disabled);
                    draw_rect(renderer, button_rects[i], button_outline);
                    draw_text_centered(font_renderer, kDatabaseButtons[i].label, button_rects[i], 18, label_color);
                }

                font_renderer.draw_text("Browse snapshots, then press Load to keep the previewed board.", layout.footer_rect.x + 8, layout.footer_rect.y + 10, 14, muted_label);
                font_renderer.draw_text("Back restores the board you had before opening this browser.", layout.footer_rect.x + 8, layout.footer_rect.y + 28, 14, muted_label);
            }
            else if (mode == ChessGuiMode::network_setup)
            {
                font_renderer.draw_text("Network game", layout.info_rect.x + 8, layout.info_rect.y + 42, info_size, label_color);

                const auto role_rects = compute_network_role_rects(layout);
                for (std::size_t i = 0; i < role_rects.size(); ++i)
                {
                    const bool selected = (static_cast<int>(i) == ((network_state.role == ChessGuiNetworkRole::host) ? 0 : 1));
                    fill_rect(renderer, role_rects[i], selected ? button_hover : menu_item_fill);
                    draw_rect(renderer, role_rects[i], button_outline);
                    draw_text_centered(font_renderer, kNetworkRoleLabels[i], role_rects[i], 18, label_color);
                }

                font_renderer.draw_text("Username", layout.panel_rect.x, layout.info_rect.y + 78, 15, muted_label);
                SDL_Rect username_rect = compute_network_username_rect(layout);
                fill_rect(renderer, username_rect, active_text_field == TextInputField::network_username ? button_hover : menu_item_fill);
                draw_rect(renderer, username_rect, button_outline);
                font_renderer.draw_text(network_state.username.empty() ? "player" : network_state.username, username_rect.x + 8, username_rect.y + 8, 17, label_color);

                if (network_state.role == ChessGuiNetworkRole::join)
                {
                    font_renderer.draw_text("Host", layout.panel_rect.x, layout.info_rect.y + 132, 15, muted_label);
                    SDL_Rect host_rect = compute_network_host_rect(layout);
                    fill_rect(renderer, host_rect, active_text_field == TextInputField::network_host ? button_hover : menu_item_fill);
                    draw_rect(renderer, host_rect, button_outline);
                    font_renderer.draw_text(network_state.host.empty() ? "127.0.0.1" : network_state.host, host_rect.x + 8, host_rect.y + 8, 17, label_color);
                }
                else
                {
                    font_renderer.draw_text("Host color", layout.panel_rect.x, layout.info_rect.y + 204, 15, muted_label);
                    const auto color_rects = compute_network_color_rects(layout);
                    for (std::size_t i = 0; i < color_rects.size(); ++i)
                    {
                        const bool selected = (static_cast<int>(i) == (network_state.host_plays_white ? 0 : 1));
                        fill_rect(renderer, color_rects[i], selected ? button_hover : menu_item_fill);
                        draw_rect(renderer, color_rects[i], button_outline);
                        draw_text_centered(font_renderer, kNetworkColorLabels[i], color_rects[i], 18, label_color);
                    }
                }

                font_renderer.draw_text("Password", layout.panel_rect.x, layout.info_rect.y + 258, 15, muted_label);
                SDL_Rect password_rect = compute_network_password_rect(layout);
                fill_rect(renderer, password_rect, active_text_field == TextInputField::network_password ? button_hover : menu_item_fill);
                draw_rect(renderer, password_rect, button_outline);
                font_renderer.draw_text(masked_password(network_state.password), password_rect.x + 8, password_rect.y + 8, 17, label_color);

                if (!network_state.status_message.empty())
                {
                    font_renderer.draw_text(network_state.status_message, layout.panel_rect.x, layout.panel_rect.y + layout.panel_rect.h - 92, 14, muted_label);
                }

                const auto button_rects = compute_network_button_rects(layout);
                for (std::size_t i = 0; i < button_rects.size(); ++i)
                {
                    fill_rect(renderer, button_rects[i], button_fill);
                    draw_rect(renderer, button_rects[i], button_outline);
                    draw_text_centered(font_renderer, kNetworkButtons[i].label, button_rects[i], 18, label_color);
                }

                font_renderer.draw_text("Choose Host or Join, fill the fields, then press Start.", layout.footer_rect.x + 8, layout.footer_rect.y + 10, 14, muted_label);
                font_renderer.draw_text("Default port comes from config.json. Password may be left empty.", layout.footer_rect.x + 8, layout.footer_rect.y + 28, 14, muted_label);
            }
            else
            {
                std::string turn_line = "No active player";
                if (snapshot.current_player == playerColor::white)
                {
                    turn_line = "White to move";
                }
                else if (snapshot.current_player == playerColor::black)
                {
                    turn_line = "Black to move";
                }
                font_renderer.draw_text(turn_line, layout.info_rect.x + 10, layout.info_rect.y + 44, info_size, label_color);
                font_renderer.draw_text(status_line(snapshot), layout.info_rect.x + 10, layout.info_rect.y + 70, info_size, label_color);
                if (snapshot.has_last_move)
                {
                    font_renderer.draw_text("Last move: " + square_name(snapshot.last_move_start) + " -> " + square_name(snapshot.last_move_dest),
                                            layout.info_rect.x + 10,
                                            layout.info_rect.y + 96,
                                            16,
                                            muted_label);
                }
                else
                {
                    font_renderer.draw_text("Last move: none", layout.info_rect.x + 10, layout.info_rect.y + 96, 16, muted_label);
                }

                std::string mode_line = "GUI main menu is ready";
                if (mode == ChessGuiMode::local_game)
                {
                    mode_line = "Drag pieces or use the side buttons";
                }
                else if (mode == ChessGuiMode::network_game)
                {
                    mode_line = "Network game active";
                }
                else if (mode == ChessGuiMode::busy)
                {
                    if (!network_state.status_message.empty())
                    {
                        mode_line = network_state.status_message;
                    }
                    else if (!database_state.status_message.empty())
                    {
                        mode_line = database_state.status_message;
                    }
                    else if (!board_editor_state.status_message.empty())
                    {
                        mode_line = board_editor_state.status_message;
                    }
                    else
                    {
                        mode_line = "Working...";
                    }
                }
                font_renderer.draw_text(mode_line, layout.footer_rect.x + 8, layout.footer_rect.y + 10, 15, muted_label);
                font_renderer.draw_text("Moves played: " + std::to_string(snapshot.move_count), layout.footer_rect.x + 8, layout.footer_rect.y + 28, 15, muted_label);

                for (std::size_t i = 0; i < kButtons.size(); ++i)
                {
                    const bool enabled = game_button_enabled(mode, kButtons[i].action);
                    SDL_Color fill = button_fill;
                    if (!enabled)
                    {
                        fill = button_disabled;
                    }
                    else if (static_cast<int>(i) == pressed_button)
                    {
                        fill = button_pressed;
                    }
                    else if (static_cast<int>(i) == hovered_button)
                    {
                        fill = button_hover;
                    }

                    fill_rect(renderer, layout.button_rects[i], fill);
                    draw_rect(renderer, layout.button_rects[i], button_outline);
                    draw_text_centered(font_renderer, kButtons[i].label, layout.button_rects[i], 20, label_color);
                }
            }

            SDL_RenderPresent(renderer);
        }

        mutable std::mutex mutex_;
        std::condition_variable init_cv_;
        GuiSnapshot snapshot_;
        ChessGuiMode mode_ = ChessGuiMode::main_menu;
        ChessGuiBoardEditorState board_editor_state_{};
        ChessGuiDatabaseState database_state_{};
        ChessGuiNetworkState network_state_{};
        std::deque<ChessGuiAction> pending_actions_;
        bool initialized_ = false;
        bool init_success_ = false;
        bool title_dirty_ = true;
        std::string init_error_;
        bool dragging_ = false;
        boardCoordinateType drag_from_{'A', 1};
        pieceType drag_piece_{pieceCode::empty, playerColor::none};
        int drag_mouse_x_ = 0;
        int drag_mouse_y_ = 0;
        int pressed_menu_index_ = -1;
        int hovered_menu_index_ = -1;
        int pressed_button_index_ = -1;
        int hovered_button_index_ = -1;
        TextInputField active_text_field_ = TextInputField::none;
        std::atomic<bool> running_{true};
        std::atomic<bool> open_{false};
        std::thread worker_;
    };
} // namespace

std::unique_ptr<ChessGui> create_chess_gui(std::string &error_message)
{
    std::unique_ptr<SdlChessGui> gui(new SdlChessGui());
    if (!gui->wait_until_initialized(error_message))
    {
        return nullptr;
    }

    return gui;
}
