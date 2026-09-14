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
    // A bit larger than the old 1020x760: several side-panel screens (board
    // editor, database browser, network setup) pack in enough text and
    // controls that the previous default left them cramped.
    constexpr int kInitialWindowWidth = 1140;
    constexpr int kInitialWindowHeight = 820;

    struct GuiSnapshot
    {
        boardType board;
        playerColor current_player = playerColor::white;
        bool white_checked = false;
        bool black_checked = false;
        bool white_checkmate = false;
        bool black_checkmate = false;
        bool stalemate = false;
        bool rule_draw = false;
        std::size_t move_count = 0;
        bool has_last_move = false;
        boardCoordinateType last_move_start{'A', 1};
        boardCoordinateType last_move_dest{'A', 1};
        std::string white_player_name;
        std::string black_player_name;
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
        network_password,
        settings_value,
        chat_message
    };

    struct MenuSpec
    {
        ChessGuiActionType action;
        const char *label;
    };

    constexpr std::array<MenuSpec, 6> kMenuItems{{
        {ChessGuiActionType::start_new_game, "New Game"},
        {ChessGuiActionType::board_editor, "Board Editor"},
        {ChessGuiActionType::load_from_database, "Load DB"},
        {ChessGuiActionType::play_current_board, "Play Current"},
        {ChessGuiActionType::start_network_game, "Network"},
        {ChessGuiActionType::open_settings, "Settings"},
    }};

    constexpr std::array<ButtonSpec, 7> kButtons{{
        {ChessGuiActionType::smart_move, "Smart"},
        {ChessGuiActionType::random_move, "Random"},
        {ChessGuiActionType::undo, "Undo"},
        {ChessGuiActionType::ml_move, "ML Move"},
        {ChessGuiActionType::list_moves, "Legal Moves"},
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
        {ChessGuiActionType::database_selection_changed, "Prev Position"},
        {ChessGuiActionType::database_selection_changed, "Next Position"},
        {ChessGuiActionType::database_load_snapshot, "Load"},
        {ChessGuiActionType::database_back, "Back"},
    }};

    constexpr std::array<const char *, 2> kNetworkRoleLabels{{"Host", "Join"}};
    constexpr std::array<const char *, 2> kNetworkColorLabels{{"White", "Black"}};
    constexpr std::array<ButtonSpec, 2> kNetworkButtons{{
        {ChessGuiActionType::network_submit, "Start"},
        {ChessGuiActionType::network_back, "Back"},
    }};

    constexpr std::array<ButtonSpec, 2> kSettingsButtons{{
        {ChessGuiActionType::settings_save, "Save"},
        {ChessGuiActionType::settings_back, "Back"},
    }};

    struct Layout
    {
        SDL_Rect menu_bar_rect{}; // the top action bar - 1 row for most modes, 2 for board_editor
        int menu_bar_rows = 1;
        SDL_Rect board_rect{};
        int square_size = 0;
        int label_margin = 0; // reserved clearance around the board for file/rank labels
        SDL_Rect panel_rect{};
        SDL_Rect info_rect{};
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

    // One row within the (possibly multi-row) top bar.
    SDL_Rect top_bar_row_rect(const Layout &layout, int row_index, int total_rows)
    {
        const int row_gap = 8;
        const int row_height = (layout.menu_bar_rect.h - ((total_rows - 1) * row_gap)) / total_rows;
        return SDL_Rect{
            layout.menu_bar_rect.x,
            layout.menu_bar_rect.y + (row_index * (row_height + row_gap)),
            layout.menu_bar_rect.w,
            row_height};
    }

    // `count` equal-width buttons laid out left to right within `row_rect`.
    std::vector<SDL_Rect> layout_button_row(const SDL_Rect &row_rect, int count)
    {
        std::vector<SDL_Rect> rects(static_cast<std::size_t>(count));
        const int gap = 8;
        const int width = std::max(60, (row_rect.w - (gap * (count - 1))) / count);
        int x = row_rect.x;
        for (int i = 0; i < count; ++i)
        {
            rects[static_cast<std::size_t>(i)] = SDL_Rect{x, row_rect.y, width, row_rect.h};
            x += width + gap;
        }
        return rects;
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

    // require_chess_charset selects between two different jobs: plain UI text
    // wants an actual sans-serif font, while chess pieces need a font that
    // covers U+2654-U+265F, which most sans fonts don't - conflating the two
    // used to make Fontconfig discard the "sans" preference for every label
    // just to satisfy the piece glyphs. Keeping the lookups separate lets each
    // pick the font suited to its job.
    bool locate_font_file(bool require_chess_charset, std::string &font_path, std::string &error_message)
    {
        if (FcInit() == 0)
        {
            error_message = "Fontconfig initialization failed.";
            return false;
        }

        FcPattern *pattern = FcPatternCreate();
        FcCharSet *charset = require_chess_charset ? FcCharSetCreate() : nullptr;
        if (pattern == nullptr || (require_chess_charset && charset == nullptr))
        {
            if (pattern != nullptr)
                FcPatternDestroy(pattern);
            if (charset != nullptr)
                FcCharSetDestroy(charset);
            error_message = "Could not allocate Fontconfig objects.";
            return false;
        }

        if (require_chess_charset)
        {
            for (FcChar32 codepoint = 0x2654; codepoint <= 0x265F; ++codepoint)
            {
                FcCharSetAddChar(charset, codepoint);
            }
            FcPatternAddCharSet(pattern, FC_CHARSET, charset);
        }

        FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>("sans"));
        FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);
        FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);

        FcResult result = FcResultNoMatch;
        FcPattern *match = FcFontMatch(nullptr, pattern, &result);
        FcPatternDestroy(pattern);
        if (charset != nullptr)
            FcCharSetDestroy(charset);

        if (match == nullptr)
        {
            error_message = require_chess_charset
                                ? "Could not find a system font with Unicode chess pieces."
                                : "Could not find a system UI font.";
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

            if (symbol_face_ != nullptr)
            {
                FT_Done_Face(symbol_face_);
            }
            if (text_face_ != nullptr)
            {
                FT_Done_Face(text_face_);
            }
            if (library_ != nullptr)
            {
                FT_Done_FreeType(library_);
            }
        }

        bool initialize(std::string &error_message)
        {
            std::string text_font_path;
            if (!locate_font_file(false, text_font_path, error_message))
            {
                return false;
            }

            if (FT_Init_FreeType(&library_) != 0)
            {
                error_message = "FreeType initialization failed.";
                return false;
            }

            if (FT_New_Face(library_, text_font_path.c_str(), 0, &text_face_) != 0)
            {
                error_message = "Could not open GUI font: " + text_font_path;
                return false;
            }

            // No font covering the chess-piece block is not fatal: get_glyph()
            // falls back to text_face_ (and then to '?') for those codepoints.
            std::string symbol_font_path;
            std::string symbol_error;
            if (locate_font_file(true, symbol_font_path, symbol_error) && symbol_font_path != text_font_path)
            {
                FT_New_Face(library_, symbol_font_path.c_str(), 0, &symbol_face_);
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

            const bool is_chess_piece = codepoint >= 0x2654 && codepoint <= 0x265F;
            FT_Face face = (is_chess_piece && symbol_face_ != nullptr) ? symbol_face_ : text_face_;

            GlyphTexture glyph;
            if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixel_size)) != 0)
            {
                return glyph_cache_.emplace(key, glyph).first->second;
            }

            FT_ULong glyph_codepoint = static_cast<FT_ULong>(codepoint);
            if (FT_Load_Char(face, glyph_codepoint, FT_LOAD_RENDER) != 0)
            {
                face = text_face_;
                if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixel_size)) != 0 ||
                    FT_Load_Char(face, static_cast<FT_ULong>('?'), FT_LOAD_RENDER) != 0)
                {
                    return glyph_cache_.emplace(key, glyph).first->second;
                }
            }

            FT_GlyphSlot slot = face->glyph;
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
        FT_Face text_face_ = nullptr;
        FT_Face symbol_face_ = nullptr;
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

    // Greedily wraps `text` at word boundaries to fit `max_width`, drawing
    // each line below the last. Used for status/tip lines whose length isn't
    // known ahead of time and would otherwise run past the panel edge.
    // Returns the y-coordinate just below the last line drawn, so callers can
    // stack further text beneath it without knowing the wrap count up front.
    int draw_wrapped_text(FontRenderer &font_renderer, const std::string &text, int x, int y, int max_width, int pixel_size, SDL_Color color, int line_gap = 4)
    {
        std::istringstream words(text);
        std::string word;
        std::string line;
        int line_y = y;
        while (words >> word)
        {
            const std::string candidate = line.empty() ? word : (line + " " + word);
            if (!line.empty() && font_renderer.measure_text(candidate, pixel_size).width > max_width)
            {
                font_renderer.draw_text(line, x, line_y, pixel_size, color);
                line_y += pixel_size + line_gap;
                line = word;
            }
            else
            {
                line = candidate;
            }
        }
        if (!line.empty())
        {
            font_renderer.draw_text(line, x, line_y, pixel_size, color);
        }
        return line_y + pixel_size + line_gap;
    }

    // Shortens `text` from the left (keeping the tail, prefixed with "...")
    // until it fits `max_width` - used for values like file paths that can
    // be far longer than the field row they share with a label.
    std::string elide_left(FontRenderer &font_renderer, const std::string &text, int max_width, int pixel_size)
    {
        if (font_renderer.measure_text(text, pixel_size).width <= max_width)
        {
            return text;
        }
        const std::string ellipsis = "...";
        for (std::size_t start = 0; start < text.size(); ++start)
        {
            const std::string candidate = ellipsis + text.substr(start);
            if (font_renderer.measure_text(candidate, pixel_size).width <= max_width)
            {
                return candidate;
            }
        }
        return ellipsis;
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
        else if (snapshot.stalemate)
        {
            oss << " | Stalemate: Draw";
        }
        else if (snapshot.rule_draw)
        {
            oss << " | Draw";
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
        if (snapshot.stalemate)
        {
            return "Stalemate: Draw";
        }
        if (snapshot.rule_draw)
        {
            return "Draw";
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

    // Sits directly below info_rect (top-down), with its own "Save name"
    // label above it. The piece palette and Clear/Default/Save/Back actions
    // live in the top bar now (see primary_top_bar_rects /
    // secondary_top_bar_rects), so this is the only editor-specific widget
    // still positioned inside the side panel.
    SDL_Rect compute_editor_save_field_rect(const Layout &layout)
    {
        const int label_y = layout.info_rect.y + layout.info_rect.h + 12;
        return SDL_Rect{layout.info_rect.x, label_y + 20, layout.info_rect.w, 34};
    }

    // Lives in the top bar, alongside the other mode-specific action rows
    // (see primary_top_bar_rects) - its click handling stays self-contained
    // since each of the 6 buttons does something different, unlike the
    // uniform "kX[index].action" dispatch the top bar otherwise uses.
    std::vector<SDL_Rect> compute_database_button_rects(const Layout &layout)
    {
        return layout_button_row(top_bar_row_rect(layout, 0, 1), static_cast<int>(kDatabaseButtons.size()));
    }

    struct NetworkFormLayout
    {
        SDL_Rect role_rects[2]{};
        int username_label_y = 0;
        SDL_Rect username_rect{};
        int host_label_y = 0;
        SDL_Rect host_rect{}; // only meaningful in join mode
        int color_label_y = 0;
        SDL_Rect color_rects[2]{}; // only meaningful in host mode
        int password_label_y = 0;
        SDL_Rect password_rect{};
        int status_y = 0;
    };

    // Every field flows top-down from the title, and Host vs. Join mode
    // swaps in the color selector or the Host field at the same point in
    // the sequence - previously each field had its own fixed offset from
    // info_rect.y, which put the "Network game" title directly under the
    // role buttons (both computed independently as "+42" and "+44") and
    // left a large dead gap in Host mode where the unused Host field's
    // reserved space still was.
    NetworkFormLayout compute_network_form_layout(const Layout &layout, ChessGuiNetworkRole role)
    {
        NetworkFormLayout result;
        const int gap = 10;
        const int section_gap = 14;
        const int width = (layout.panel_rect.w - gap) / 2;
        const int toggle_height = 36;
        const int row_height = 38;
        const int label_gap = 20;

        int y = layout.info_rect.y + 76; // below the title, with room for its full glyph height

        result.role_rects[0] = SDL_Rect{layout.panel_rect.x, y, width, toggle_height};
        result.role_rects[1] = SDL_Rect{layout.panel_rect.x + width + gap, y, width, toggle_height};
        y += toggle_height + section_gap;

        result.username_label_y = y;
        y += label_gap;
        result.username_rect = SDL_Rect{layout.panel_rect.x, y, layout.panel_rect.w, row_height};
        y += row_height + section_gap;

        if (role == ChessGuiNetworkRole::join)
        {
            result.host_label_y = y;
            y += label_gap;
            result.host_rect = SDL_Rect{layout.panel_rect.x, y, layout.panel_rect.w, row_height};
            y += row_height + section_gap;
        }
        else
        {
            result.color_label_y = y;
            y += label_gap;
            result.color_rects[0] = SDL_Rect{layout.panel_rect.x, y, width, toggle_height};
            result.color_rects[1] = SDL_Rect{layout.panel_rect.x + width + gap, y, width, toggle_height};
            y += toggle_height + section_gap;
        }

        result.password_label_y = y;
        y += label_gap;
        result.password_rect = SDL_Rect{layout.panel_rect.x, y, layout.panel_rect.w, row_height};
        y += row_height + section_gap;

        result.status_y = y;
        return result;
    }

    // Start/Back live in the top bar; role, color, and the text fields below
    // stay in the side panel since they form one cohesive input form.
    std::vector<SDL_Rect> compute_network_button_rects(const Layout &layout)
    {
        return layout_button_row(top_bar_row_rect(layout, 0, 1), static_cast<int>(kNetworkButtons.size()));
    }

    // One row per config field, stacked in the side panel between the info
    // header and the footer. Row height adapts to whatever room is actually
    // available: capped for looks on a tall window, but deliberately with no
    // lower floor - a floor here would do exactly what a fixed height did to
    // the old editor palette (silently overlap the footer) once 16 rows
    // don't fit even at the floor, which happens at the enforced minimum
    // window size.
    std::vector<SDL_Rect> compute_settings_field_rects(const Layout &layout, int count)
    {
        const int gap = 4;
        const int top = layout.info_rect.y + layout.info_rect.h + 10;
        const int bottom = layout.footer_rect.y - 10;
        const int available = bottom - top;
        const int row_height = std::min(30, std::max(1, (available - (gap * (count - 1))) / count));

        std::vector<SDL_Rect> rects(static_cast<std::size_t>(count));
        int y = top;
        for (int i = 0; i < count; ++i)
        {
            rects[static_cast<std::size_t>(i)] = SDL_Rect{layout.panel_rect.x, y, layout.panel_rect.w, row_height};
            y += row_height + gap;
        }
        return rects;
    }

    // Anchored above the footer (fixed height, independent of how tall the
    // chat history or the game-action message above it happen to be) so
    // both rendering and click hit-testing can compute it identically
    // without needing to know how much text was drawn above it this frame.
    SDL_Rect compute_chat_input_rect(const Layout &layout)
    {
        const int height = 30;
        return SDL_Rect{layout.info_rect.x + 8, layout.footer_rect.y - height - 10, layout.info_rect.w - 16, height};
    }

    // Board editor needs two action rows in the top bar (piece palette, then
    // Clear/Default/Save/Back); every other mode needs exactly one.
    int top_bar_rows_for(ChessGuiMode mode)
    {
        return mode == ChessGuiMode::board_editor ? 2 : 1;
    }

    Layout compute_layout(int width, int height, ChessGuiMode mode)
    {
        Layout layout;
        const int padding = std::max(18, std::min(width, height) / 28);
        // Reserved on every side of the board for the file/rank coordinate
        // labels, in addition to `padding`, so the labels always have their
        // own dedicated room and can never overlap the menu bar, the side
        // panel, or the window edge, regardless of window size or aspect
        // ratio (previously they used fixed pixel offsets that only fit at
        // the default window size).
        const int label_margin = std::max(20, padding);
        layout.label_margin = label_margin;
        const int board_gap = padding + label_margin;
        const int menu_row_gap = 8;
        const int menu_row_height = 42;
        const int menu_bar_rows = top_bar_rows_for(mode);
        layout.menu_bar_rows = menu_bar_rows;
        layout.menu_bar_rect = SDL_Rect{
            padding,
            padding,
            std::max(260, width - (2 * padding)),
            (menu_bar_rows * menu_row_height) + ((menu_bar_rows - 1) * menu_row_gap)};

        const int min_board_size = 8 * 42;
        const int content_top = layout.menu_bar_rect.y + layout.menu_bar_rect.h + board_gap;
        int panel_width = std::clamp(width / 4, 210, 270);
        int board_size = std::min(height - content_top - board_gap, width - (2 * board_gap) - panel_width - padding);
        board_size = std::max(min_board_size, (board_size / 8) * 8);

        if ((2 * board_gap) + board_size + panel_width + padding > width)
        {
            panel_width = std::max(180, width - (2 * board_gap) - board_size - padding);
        }

        board_size = std::min(board_size, height - content_top - board_gap);
        board_size = (board_size / 8) * 8;

        layout.square_size = board_size / 8;
        const int content_height = std::max(board_size, height - content_top - board_gap);
        layout.board_rect = SDL_Rect{board_gap, content_top + ((content_height - board_size) / 2), board_size, board_size};
        layout.panel_rect = SDL_Rect{
            layout.board_rect.x + layout.board_rect.w + board_gap,
            content_top,
            std::max(180, width - layout.board_rect.x - layout.board_rect.w - board_gap - padding),
            std::max(160, height - content_top - padding)};

        const int info_height = std::clamp(layout.panel_rect.h / 4, 110, 150);
        layout.info_rect = SDL_Rect{
            layout.panel_rect.x,
            layout.panel_rect.y,
            layout.panel_rect.w,
            info_height};

        // Generous on purpose: a couple of footer hints are two full
        // sentences, and without exact glyph metrics to hand it's safer to
        // budget for both wrapping to 2 lines each (4 total) than to tune
        // text length against a tight guess and get bitten again.
        const int footer_height = 90;
        layout.footer_rect = SDL_Rect{
            layout.panel_rect.x,
            layout.panel_rect.y + layout.panel_rect.h - footer_height,
            layout.panel_rect.w,
            footer_height};

        return layout;
    }

    // The top bar's main row of buttons, which differs entirely by mode: the
    // main-menu screens, the in-game quick actions, and the board editor's
    // piece palette all live here (database browser and network setup lay
    // out their own button rows directly, since their click handling was
    // already self-contained).
    std::vector<SDL_Rect> primary_top_bar_rects(const Layout &layout, ChessGuiMode mode)
    {
        switch (mode)
        {
        case ChessGuiMode::main_menu:
            return layout_button_row(top_bar_row_rect(layout, 0, 1), static_cast<int>(kMenuItems.size()));
        case ChessGuiMode::local_game:
        case ChessGuiMode::network_game:
            return layout_button_row(top_bar_row_rect(layout, 0, 1), static_cast<int>(kButtons.size()));
        case ChessGuiMode::board_editor:
            return layout_button_row(top_bar_row_rect(layout, 0, 2), static_cast<int>(kEditorPalette.size()));
        case ChessGuiMode::settings:
            return layout_button_row(top_bar_row_rect(layout, 0, 1), static_cast<int>(kSettingsButtons.size()));
        default:
            return {};
        }
    }

    // The top bar's second row - only the board editor uses one, for its
    // Clear/Default/Save/Back actions below the piece palette.
    std::vector<SDL_Rect> secondary_top_bar_rects(const Layout &layout, ChessGuiMode mode)
    {
        if (mode == ChessGuiMode::board_editor)
        {
            return layout_button_row(top_bar_row_rect(layout, 1, 2), static_cast<int>(kEditorButtons.size()));
        }
        return {};
    }

    int rect_index_at(const std::vector<SDL_Rect> &rects, int x, int y)
    {
        for (std::size_t i = 0; i < rects.size(); ++i)
        {
            if (point_in_rect(x, y, rects[i]))
            {
                return static_cast<int>(i);
            }
        }

        return -1;
    }

    int menu_index_at(const Layout &layout, ChessGuiMode mode, int x, int y)
    {
        return rect_index_at(primary_top_bar_rects(layout, mode), x, y);
    }

    int button_index_at(const Layout &layout, ChessGuiMode mode, int x, int y)
    {
        return rect_index_at(secondary_top_bar_rects(layout, mode), x, y);
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
        {
            snapshot_.board = make_empty_board();
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
            GuiSnapshot next;
            next.board = game.board();
            next.current_player = game.current_player_color();
            next.white_checked = game.is_checked(playerColor::white);
            next.black_checked = game.is_checked(playerColor::black);
            next.white_checkmate = game.is_checkmate(playerColor::white);
            next.black_checkmate = game.is_checkmate(playerColor::black);
            next.stalemate = game.is_stalemate(playerColor::white) || game.is_stalemate(playerColor::black);
            next.rule_draw = game.is_draw();
            next.move_count = game.move_count();
            next.has_last_move = game.has_played_moves();
            next.white_player_name = game.player_name(playerColor::white);
            next.black_player_name = game.player_name(playerColor::black);

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
            if (mode_ != ChessGuiMode::network_game)
            {
                local_player_color_ = playerColor::none;
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

        void set_settings_state(const ChessGuiSettingsState &state) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            settings_state_ = state;
            title_dirty_ = true;
        }

        ChessGuiSettingsState settings_state() const override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return settings_state_;
        }

        void set_game_action_state(const ChessGuiGameActionState &state) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            game_action_state_ = state;
            title_dirty_ = true;
        }

        ChessGuiGameActionState game_action_state() const override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return game_action_state_;
        }

        void set_chat_state(const ChessGuiChatState &state) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            chat_state_ = state;
            title_dirty_ = true;
        }

        ChessGuiChatState chat_state() const override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return chat_state_;
        }

        void set_local_player_color(playerColor color) override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            local_player_color_ = color;
        }

        playerColor local_player_color() const override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return local_player_color_;
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
            SDL_SetWindowMinimumSize(window, 900, 700);

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

                GuiSnapshot snapshot_copy;
                ChessGuiMode mode_copy = ChessGuiMode::main_menu;
                ChessGuiBoardEditorState editor_state_copy;
                ChessGuiDatabaseState database_state_copy;
                ChessGuiNetworkState network_state_copy;
                ChessGuiSettingsState settings_state_copy;
                ChessGuiGameActionState game_action_state_copy;
                ChessGuiChatState chat_state_copy;
                playerColor local_player_color_copy = playerColor::none;
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
                    settings_state_copy = settings_state_;
                    game_action_state_copy = game_action_state_;
                    chat_state_copy = chat_state_;
                    local_player_color_copy = local_player_color_;
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
                                settings_state_copy,
                                game_action_state_copy,
                                chat_state_copy,
                                local_player_color_copy,
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
            case TextInputField::settings_value:
                if (settings_state_.selected_field_index >= 0 &&
                    static_cast<std::size_t>(settings_state_.selected_field_index) < settings_state_.fields.size())
                {
                    return &settings_state_.fields[static_cast<std::size_t>(settings_state_.selected_field_index)].value;
                }
                return nullptr;
            case TextInputField::chat_message:
                return &chat_state_.pending_input;
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

        // Shared by the database browser's mouse buttons and arrow-key
        // handling so both paths move/refresh in exactly the same way.
        void step_database_game_locked(int direction)
        {
            if (direction < 0 && database_state_.selected_game_index > 0)
            {
                database_state_.selected_game_index--;
                database_state_.selected_snapshot_index = 0;
                pending_actions_.push_back(ChessGuiAction{ChessGuiActionType::database_selection_changed, {'A', 1}, {'A', 1}});
            }
            else if (direction > 0 && database_state_.selected_game_index + 1 < static_cast<int>(database_state_.games.size()))
            {
                database_state_.selected_game_index++;
                database_state_.selected_snapshot_index = 0;
                pending_actions_.push_back(ChessGuiAction{ChessGuiActionType::database_selection_changed, {'A', 1}, {'A', 1}});
            }
        }

        void step_database_snapshot_locked(int direction)
        {
            if (direction < 0 && database_state_.selected_snapshot_index > 0)
            {
                database_state_.selected_snapshot_index--;
                pending_actions_.push_back(ChessGuiAction{ChessGuiActionType::database_selection_changed, {'A', 1}, {'A', 1}});
            }
            else if (direction > 0 && database_state_.selected_snapshot_index + 1 < database_state_.snapshot_count)
            {
                database_state_.selected_snapshot_index++;
                pending_actions_.push_back(ChessGuiAction{ChessGuiActionType::database_selection_changed, {'A', 1}, {'A', 1}});
            }
        }

        void handle_event(const SDL_Event &event, SDL_Window *window)
        {
            int width = 0;
            int height = 0;
            SDL_GetWindowSize(window, &width, &height);

            std::lock_guard<std::mutex> lock(mutex_);
            // mode_ is only safe to read under the lock, and the top bar's
            // row count depends on it, so layout is computed after locking.
            const Layout layout = compute_layout(width, height, mode_);

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
                else if (active_text_field_ == TextInputField::chat_message &&
                         (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER))
                {
                    // The actual send (and clearing pending_input) happens on
                    // the network thread once it picks up this action, so it
                    // can read the exact text that was typed rather than
                    // racing a GUI-side clear against it.
                    if (!chat_state_.pending_input.empty())
                    {
                        pending_actions_.push_back(ChessGuiAction{ChessGuiActionType::send_chat, {'A', 1}, {'A', 1}});
                    }
                }
                else if (mode_ == ChessGuiMode::database_browser)
                {
                    switch (event.key.keysym.sym)
                    {
                    case SDLK_UP:
                        step_database_game_locked(-1);
                        break;
                    case SDLK_DOWN:
                        step_database_game_locked(1);
                        break;
                    case SDLK_LEFT:
                        step_database_snapshot_locked(-1);
                        break;
                    case SDLK_RIGHT:
                        step_database_snapshot_locked(1);
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        if (database_state_.snapshot_count > 0)
                        {
                            pending_actions_.push_back(ChessGuiAction{ChessGuiActionType::database_load_snapshot, {'A', 1}, {'A', 1}});
                        }
                        break;
                    case SDLK_ESCAPE:
                        pending_actions_.push_back(ChessGuiAction{ChessGuiActionType::database_back, {'A', 1}, {'A', 1}});
                        break;
                    default:
                        break;
                    }
                }
                return;
            }

            if (event.type == SDL_MOUSEMOTION)
            {
                drag_mouse_x_ = event.motion.x;
                drag_mouse_y_ = event.motion.y;
                hovered_menu_index_ = menu_index_at(layout, mode_, event.motion.x, event.motion.y);
                hovered_button_index_ = button_index_at(layout, mode_, event.motion.x, event.motion.y);
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
            hovered_menu_index_ = menu_index_at(layout, mode_, mouse_x, mouse_y);
            hovered_button_index_ = button_index_at(layout, mode_, mouse_x, mouse_y);

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
                const int menu_index = menu_index_at(layout, mode_, mouse_x, mouse_y);
                if (menu_index >= 0)
                {
                    bool clickable = (mode_ == ChessGuiMode::main_menu) || (mode_ == ChessGuiMode::board_editor) ||
                                      (mode_ == ChessGuiMode::settings);
                    if (!clickable && (mode_ == ChessGuiMode::local_game || mode_ == ChessGuiMode::network_game))
                    {
                        clickable = game_button_enabled(mode_, kButtons[static_cast<std::size_t>(menu_index)].action);
                    }
                    if (clickable)
                    {
                        pressed_menu_index_ = menu_index;
                        return;
                    }
                }

                const int button_index = button_index_at(layout, mode_, mouse_x, mouse_y);
                if (button_index >= 0 && mode_ == ChessGuiMode::board_editor)
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
                else if (mode_ == ChessGuiMode::settings)
                {
                    const auto field_rects = compute_settings_field_rects(layout, static_cast<int>(settings_state_.fields.size()));
                    for (std::size_t i = 0; i < field_rects.size(); ++i)
                    {
                        if (!point_in_rect(mouse_x, mouse_y, field_rects[i]))
                        {
                            continue;
                        }
                        if (settings_state_.fields[i].is_bool)
                        {
                            settings_state_.fields[i].value = (settings_state_.fields[i].value == "yes") ? "no" : "yes";
                            active_text_field_ = TextInputField::none;
                            settings_state_.selected_field_index = -1;
                        }
                        else
                        {
                            settings_state_.selected_field_index = static_cast<int>(i);
                            active_text_field_ = TextInputField::settings_value;
                        }
                        return;
                    }
                    return;
                }
                else if (mode_ == ChessGuiMode::network_setup)
                {
                    const NetworkFormLayout form = compute_network_form_layout(layout, network_state_.role);
                    if (point_in_rect(mouse_x, mouse_y, form.username_rect))
                    {
                        active_text_field_ = TextInputField::network_username;
                        return;
                    }
                    if (network_state_.role == ChessGuiNetworkRole::join &&
                        point_in_rect(mouse_x, mouse_y, form.host_rect))
                    {
                        active_text_field_ = TextInputField::network_host;
                        return;
                    }
                    if (point_in_rect(mouse_x, mouse_y, form.password_rect))
                    {
                        active_text_field_ = TextInputField::network_password;
                        return;
                    }
                }
                else if (mode_ != ChessGuiMode::local_game && mode_ != ChessGuiMode::network_game)
                {
                    return;
                }

                if (mode_ == ChessGuiMode::network_game && point_in_rect(mouse_x, mouse_y, compute_chat_input_rect(layout)))
                {
                    active_text_field_ = TextInputField::chat_message;
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
                if (mode_ == ChessGuiMode::network_game && local_player_color_ != playerColor::none &&
                    piece.color != local_player_color_)
                {
                    // Not our color in this network game: don't let the drag pick up the opponent's piece.
                    return;
                }

                dragging_ = true;
                drag_from_ = square;
                drag_piece_ = piece;
                return;
            }

            if (pressed_menu_index_ >= 0)
            {
                const int released_on = menu_index_at(layout, mode_, mouse_x, mouse_y);
                if (released_on == pressed_menu_index_)
                {
                    if (mode_ == ChessGuiMode::main_menu)
                    {
                        pending_actions_.push_back(ChessGuiAction{kMenuItems[static_cast<std::size_t>(released_on)].action, {'A', 1}, {'A', 1}});
                    }
                    else if (mode_ == ChessGuiMode::local_game || mode_ == ChessGuiMode::network_game)
                    {
                        if (game_button_enabled(mode_, kButtons[static_cast<std::size_t>(released_on)].action))
                        {
                            pending_actions_.push_back(ChessGuiAction{kButtons[static_cast<std::size_t>(released_on)].action, {'A', 1}, {'A', 1}});
                        }
                    }
                    else if (mode_ == ChessGuiMode::board_editor)
                    {
                        board_editor_state_.selected_piece = kEditorPalette[static_cast<std::size_t>(released_on)].piece;
                    }
                    else if (mode_ == ChessGuiMode::settings)
                    {
                        pending_actions_.push_back(ChessGuiAction{kSettingsButtons[static_cast<std::size_t>(released_on)].action, {'A', 1}, {'A', 1}});
                    }
                }
                pressed_menu_index_ = -1;
                return;
            }

            if (pressed_button_index_ >= 0)
            {
                const int released_on = button_index_at(layout, mode_, mouse_x, mouse_y);
                if (released_on == pressed_button_index_ && mode_ == ChessGuiMode::board_editor)
                {
                    pending_actions_.push_back(ChessGuiAction{kEditorButtons[static_cast<std::size_t>(released_on)].action, {'A', 1}, {'A', 1}});
                }
                pressed_button_index_ = -1;
                return;
            }

            if (mode_ == ChessGuiMode::board_editor)
            {
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

                    if (i == 0)
                    {
                        step_database_game_locked(-1);
                    }
                    else if (i == 1)
                    {
                        step_database_game_locked(1);
                    }
                    else if (i == 2)
                    {
                        step_database_snapshot_locked(-1);
                    }
                    else if (i == 3)
                    {
                        step_database_snapshot_locked(1);
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
                const NetworkFormLayout form = compute_network_form_layout(layout, network_state_.role);
                if (point_in_rect(mouse_x, mouse_y, form.role_rects[0]))
                {
                    network_state_.role = ChessGuiNetworkRole::host;
                    active_text_field_ = TextInputField::network_username;
                    return;
                }
                if (point_in_rect(mouse_x, mouse_y, form.role_rects[1]))
                {
                    network_state_.role = ChessGuiNetworkRole::join;
                    active_text_field_ = TextInputField::network_username;
                    return;
                }

                if (network_state_.role == ChessGuiNetworkRole::host)
                {
                    if (point_in_rect(mouse_x, mouse_y, form.color_rects[0]))
                    {
                        network_state_.host_plays_white = true;
                        return;
                    }
                    if (point_in_rect(mouse_x, mouse_y, form.color_rects[1]))
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
                             const ChessGuiSettingsState &settings_state,
                             const ChessGuiGameActionState &game_action_state,
                             const ChessGuiChatState &chat_state,
                             playerColor local_player_color,
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
            const Layout layout = compute_layout(width, height, mode);

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
            const SDL_Color menu_item_fill = make_color(47, 73, 92);
            const SDL_Color menu_item_hover = make_color(62, 93, 116);
            const SDL_Color menu_item_pressed = make_color(37, 60, 77);
            const SDL_Color menu_outline = make_color(113, 144, 168);
            const SDL_Color button_fill = make_color(55, 102, 140);
            const SDL_Color button_hover = make_color(69, 123, 166);
            const SDL_Color button_pressed = make_color(44, 84, 116);
            const SDL_Color button_disabled = make_color(64, 73, 81);
            const SDL_Color button_outline = make_color(124, 154, 177);
            const SDL_Color panel_outline = make_color(77, 100, 118);

            set_draw_color(renderer, background);
            SDL_RenderClear(renderer);

            // The top bar always shows whichever action set is relevant to
            // the current screen - previously it was either the main-menu
            // buttons (main_menu) or empty (every other mode), while a
            // separate, cramped button column lived in the side panel. No
            // background box behind the buttons themselves: menu_bar_rect is
            // wider than the buttons combined once you account for integer-
            // division remainder, and filling/outlining it as its own rect
            // left a stray sliver visible past the last button - the buttons
            // already fully draw their own fill and outline.
            if (mode == ChessGuiMode::main_menu)
            {
                const auto rects = primary_top_bar_rects(layout, mode);
                for (std::size_t i = 0; i < rects.size(); ++i)
                {
                    SDL_Color fill = menu_item_fill;
                    if (static_cast<int>(i) == pressed_menu)
                    {
                        fill = menu_item_pressed;
                    }
                    else if (static_cast<int>(i) == hovered_menu)
                    {
                        fill = menu_item_hover;
                    }

                    fill_rect(renderer, rects[i], fill);
                    draw_rect(renderer, rects[i], menu_outline);
                    draw_text_centered(font_renderer, kMenuItems[i].label, rects[i], 18, label_color);
                }
            }
            else if (mode == ChessGuiMode::local_game || mode == ChessGuiMode::network_game)
            {
                const auto rects = primary_top_bar_rects(layout, mode);
                for (std::size_t i = 0; i < rects.size(); ++i)
                {
                    const bool enabled = game_button_enabled(mode, kButtons[i].action);
                    const bool is_quit = (kButtons[i].action == ChessGuiActionType::quit_game);
                    SDL_Color fill = button_fill;
                    if (!enabled)
                    {
                        fill = button_disabled;
                    }
                    else if (static_cast<int>(i) == pressed_menu)
                    {
                        fill = is_quit ? make_color(95, 38, 43) : button_pressed;
                    }
                    else if (static_cast<int>(i) == hovered_menu)
                    {
                        fill = is_quit ? make_color(145, 62, 68) : button_hover;
                    }
                    else if (is_quit)
                    {
                        fill = make_color(120, 50, 55);
                    }

                    fill_rect(renderer, rects[i], fill);
                    draw_rect(renderer, rects[i], button_outline);
                    draw_text_centered(font_renderer, kButtons[i].label, rects[i], 18, label_color);
                }
            }
            else if (mode == ChessGuiMode::board_editor)
            {
                const auto palette_rects = primary_top_bar_rects(layout, mode);
                for (std::size_t i = 0; i < palette_rects.size(); ++i)
                {
                    SDL_Color fill = same_piece(board_editor_state.selected_piece, kEditorPalette[i].piece) ? button_hover : menu_item_fill;
                    if (static_cast<int>(i) == pressed_menu)
                    {
                        fill = menu_item_pressed;
                    }
                    else if (static_cast<int>(i) == hovered_menu)
                    {
                        fill = menu_item_hover;
                    }
                    fill_rect(renderer, palette_rects[i], fill);
                    draw_rect(renderer, palette_rects[i], button_outline);

                    const std::string symbol = piece_symbol_utf8(kEditorPalette[i].piece);
                    if (!symbol.empty())
                    {
                        const SDL_Color piece_fill = (kEditorPalette[i].piece.color == playerColor::white) ? white_piece_fill : black_piece_fill;
                        const SDL_Color piece_outline = (kEditorPalette[i].piece.color == playerColor::white) ? white_piece_outline : black_piece_outline;
                        const int glyph_size = std::max(18, palette_rects[i].h - 14);
                        const TextMetrics metrics = font_renderer.measure_text(symbol, glyph_size);
                        draw_text_with_outline(font_renderer,
                                               symbol,
                                               palette_rects[i].x + std::max(0, (palette_rects[i].w - metrics.width) / 2),
                                               palette_rects[i].y + std::max(0, (palette_rects[i].h - metrics.height) / 2),
                                               glyph_size,
                                               piece_fill,
                                               piece_outline,
                                               1);
                    }
                    else
                    {
                        draw_text_centered(font_renderer, "Erase", palette_rects[i], 13, label_color);
                    }
                }

                const auto action_rects = secondary_top_bar_rects(layout, mode);
                for (std::size_t i = 0; i < action_rects.size(); ++i)
                {
                    SDL_Color fill = button_fill;
                    if (static_cast<int>(i) == pressed_button)
                    {
                        fill = button_pressed;
                    }
                    else if (static_cast<int>(i) == hovered_button)
                    {
                        fill = button_hover;
                    }
                    fill_rect(renderer, action_rects[i], fill);
                    draw_rect(renderer, action_rects[i], button_outline);
                    draw_text_centered(font_renderer, kEditorButtons[i].label, action_rects[i], 18, label_color);
                }
            }
            else if (mode == ChessGuiMode::database_browser)
            {
                const auto rects = compute_database_button_rects(layout);
                for (std::size_t i = 0; i < rects.size(); ++i)
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

                    fill_rect(renderer, rects[i], enabled ? button_fill : button_disabled);
                    draw_rect(renderer, rects[i], button_outline);
                    draw_text_centered(font_renderer, kDatabaseButtons[i].label, rects[i], 18, label_color);
                }
            }
            else if (mode == ChessGuiMode::network_setup)
            {
                const auto rects = compute_network_button_rects(layout);
                for (std::size_t i = 0; i < rects.size(); ++i)
                {
                    fill_rect(renderer, rects[i], button_fill);
                    draw_rect(renderer, rects[i], button_outline);
                    draw_text_centered(font_renderer, kNetworkButtons[i].label, rects[i], 18, label_color);
                }
            }
            else if (mode == ChessGuiMode::settings)
            {
                const auto rects = primary_top_bar_rects(layout, mode);
                for (std::size_t i = 0; i < rects.size(); ++i)
                {
                    SDL_Color fill = button_fill;
                    if (static_cast<int>(i) == pressed_menu)
                    {
                        fill = button_pressed;
                    }
                    else if (static_cast<int>(i) == hovered_menu)
                    {
                        fill = button_hover;
                    }
                    fill_rect(renderer, rects[i], fill);
                    draw_rect(renderer, rects[i], button_outline);
                    draw_text_centered(font_renderer, kSettingsButtons[i].label, rects[i], 18, label_color);
                }
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
                        fill_rect(renderer, square_rect, make_color(218, 68, 83, 90));
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
                        fill_rect(renderer, square_rect, make_color(100, 160, 220, 70));
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

            // Labels live entirely inside the board's reserved label_margin
            // clearance (see compute_layout), split into a small fixed gap
            // right against the board edge plus the remaining thickness for
            // the text itself - guaranteed to fit at any window size.
            const int label_size = std::max(14, layout.square_size / 4);
            const int label_edge_gap = 4;
            const int label_thickness = std::max(14, layout.label_margin - label_edge_gap);
            for (int file = 0; file < 8; ++file)
            {
                SDL_Rect top_rect{
                    layout.board_rect.x + (file * layout.square_size),
                    layout.board_rect.y - label_edge_gap - label_thickness,
                    layout.square_size,
                    label_thickness};
                SDL_Rect bottom_rect{
                    layout.board_rect.x + (file * layout.square_size),
                    layout.board_rect.y + layout.board_rect.h + label_edge_gap,
                    layout.square_size,
                    label_thickness};
                const std::string label(1, static_cast<char>('A' + file));
                draw_text_centered(font_renderer, label, top_rect, label_size, label_color);
                draw_text_centered(font_renderer, label, bottom_rect, label_size, label_color);
            }

            for (int rank = 8; rank >= 1; --rank)
            {
                SDL_Rect left_rect{
                    layout.board_rect.x - label_edge_gap - label_thickness,
                    layout.board_rect.y + ((8 - rank) * layout.square_size),
                    label_thickness,
                    layout.square_size};
                SDL_Rect right_rect{
                    layout.board_rect.x + layout.board_rect.w + label_edge_gap,
                    layout.board_rect.y + ((8 - rank) * layout.square_size),
                    label_thickness,
                    layout.square_size};
                const std::string label = std::to_string(rank);
                draw_text_centered(font_renderer, label, left_rect, label_size, label_color);
                draw_text_centered(font_renderer, label, right_rect, label_size, label_color);
            }

            const int title_size = 28;
            SDL_Rect title_rect{layout.info_rect.x, layout.info_rect.y, layout.info_rect.w, 34};
            draw_text_centered(font_renderer, "MATE GUI", title_rect, title_size, label_color);
            {
                SDL_Rect title_divider{layout.info_rect.x + 8, layout.info_rect.y + 36, layout.info_rect.w - 16, 1};
                fill_rect(renderer, title_divider, panel_outline);
            }

            const int info_size = 18;
            if (mode == ChessGuiMode::board_editor)
            {
                font_renderer.draw_text("Board editor", layout.info_rect.x + 8, layout.info_rect.y + 42, info_size, label_color);
                font_renderer.draw_text("Selected: " + editor_piece_label(board_editor_state.selected_piece),
                                        layout.info_rect.x + 8,
                                        layout.info_rect.y + 68,
                                        16,
                                        label_color);
                draw_wrapped_text(font_renderer,
                                  trimmed_copy(board_editor_state.status_message).empty() ? "Click a square to place the selected piece." : board_editor_state.status_message,
                                  layout.info_rect.x + 8,
                                  layout.info_rect.y + 92,
                                  layout.info_rect.w - 16,
                                  15,
                                  muted_label);

                font_renderer.draw_text("Save name", layout.info_rect.x + 8, layout.info_rect.y + layout.info_rect.h + 12, 15, muted_label);
                const SDL_Rect save_rect = compute_editor_save_field_rect(layout);
                const SDL_Color field_fill = (active_text_field == TextInputField::editor_save_name) ? button_hover : menu_item_fill;
                fill_rect(renderer, save_rect, field_fill);
                draw_rect(renderer, save_rect, button_outline);
                font_renderer.draw_text(board_editor_state.save_name.empty() ? "Custom_Board" : board_editor_state.save_name,
                                        save_rect.x + 8,
                                        save_rect.y + 8,
                                        17,
                                        label_color);

                {
                    const int footer_x = layout.footer_rect.x + 8;
                    const int footer_max_width = layout.footer_rect.w - 16;
                    int footer_y = draw_wrapped_text(font_renderer, "Use Save to write the current board to the database.", footer_x, layout.footer_rect.y + 10, footer_max_width, 14, muted_label);
                    draw_wrapped_text(font_renderer, "Tab switches fields. Back returns to the main menu.", footer_x, footer_y, footer_max_width, 14, muted_label);
                }
            }
            else if (mode == ChessGuiMode::database_browser)
            {
                const int info_max_width = layout.info_rect.w - 16;
                font_renderer.draw_text("Load from database", layout.info_rect.x + 8, layout.info_rect.y + 42, info_size, label_color);
                if (database_state.games.empty() || database_state.selected_game_index < 0)
                {
                    draw_wrapped_text(font_renderer, database_state.status_message.empty() ? "No saved games found." : database_state.status_message,
                                      layout.info_rect.x + 8, layout.info_rect.y + 72, info_max_width, 16, muted_label);
                }
                else
                {
                    const ChessGuiDatabaseEntry &game_entry = database_state.games[static_cast<std::size_t>(database_state.selected_game_index)];
                    draw_wrapped_text(font_renderer, game_entry.name, layout.info_rect.x + 8, layout.info_rect.y + 68, info_max_width, 18, label_color);
                    font_renderer.draw_text("Game " + std::to_string(database_state.selected_game_index + 1) + " of " + std::to_string(database_state.games.size()) +
                                                " | Snapshots: " + std::to_string(std::max(0, database_state.snapshot_count)),
                                            layout.info_rect.x + 8,
                                            layout.info_rect.y + 92,
                                            15,
                                            muted_label);
                    font_renderer.draw_text("Position " + std::to_string(database_state.selected_snapshot_index + 1) + " of " + std::to_string(std::max(1, database_state.snapshot_count)),
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
                    // The game/position/move-label block above ends by
                    // roughly info_rect.y+150 at most; the buttons that used
                    // to anchor this moved to the top bar, so there's no
                    // longer anything below it to collide with.
                    draw_wrapped_text(font_renderer, database_state.status_message,
                                      layout.info_rect.x + 8, layout.info_rect.y + 160,
                                      info_max_width, 14, muted_label);
                }

                {
                    const int footer_x = layout.footer_rect.x + 8;
                    const int footer_max_width = layout.footer_rect.w - 16;
                    draw_wrapped_text(font_renderer, "Arrows browse games/positions, Enter loads, Back/Escape exits.", footer_x, layout.footer_rect.y + 10, footer_max_width, 14, muted_label);
                }
            }
            else if (mode == ChessGuiMode::network_setup)
            {
                font_renderer.draw_text("Network game", layout.info_rect.x + 8, layout.info_rect.y + 42, info_size, label_color);

                const NetworkFormLayout form = compute_network_form_layout(layout, network_state.role);

                for (std::size_t i = 0; i < 2; ++i)
                {
                    const bool selected = (static_cast<int>(i) == ((network_state.role == ChessGuiNetworkRole::host) ? 0 : 1));
                    fill_rect(renderer, form.role_rects[i], selected ? button_hover : menu_item_fill);
                    draw_rect(renderer, form.role_rects[i], button_outline);
                    draw_text_centered(font_renderer, kNetworkRoleLabels[i], form.role_rects[i], 18, label_color);
                }

                font_renderer.draw_text("Username", layout.panel_rect.x, form.username_label_y, 15, muted_label);
                fill_rect(renderer, form.username_rect, active_text_field == TextInputField::network_username ? button_hover : menu_item_fill);
                draw_rect(renderer, form.username_rect, button_outline);
                font_renderer.draw_text(network_state.username.empty() ? "player" : network_state.username, form.username_rect.x + 8, form.username_rect.y + 8, 17, label_color);

                if (network_state.role == ChessGuiNetworkRole::join)
                {
                    font_renderer.draw_text("Host", layout.panel_rect.x, form.host_label_y, 15, muted_label);
                    fill_rect(renderer, form.host_rect, active_text_field == TextInputField::network_host ? button_hover : menu_item_fill);
                    draw_rect(renderer, form.host_rect, button_outline);
                    font_renderer.draw_text(network_state.host.empty() ? "127.0.0.1" : network_state.host, form.host_rect.x + 8, form.host_rect.y + 8, 17, label_color);
                }
                else
                {
                    font_renderer.draw_text("Host color", layout.panel_rect.x, form.color_label_y, 15, muted_label);
                    for (std::size_t i = 0; i < 2; ++i)
                    {
                        const bool selected = (static_cast<int>(i) == (network_state.host_plays_white ? 0 : 1));
                        fill_rect(renderer, form.color_rects[i], selected ? button_hover : menu_item_fill);
                        draw_rect(renderer, form.color_rects[i], button_outline);
                        draw_text_centered(font_renderer, kNetworkColorLabels[i], form.color_rects[i], 18, label_color);
                    }
                }

                font_renderer.draw_text("Password", layout.panel_rect.x, form.password_label_y, 15, muted_label);
                fill_rect(renderer, form.password_rect, active_text_field == TextInputField::network_password ? button_hover : menu_item_fill);
                draw_rect(renderer, form.password_rect, button_outline);
                font_renderer.draw_text(masked_password(network_state.password), form.password_rect.x + 8, form.password_rect.y + 8, 17, label_color);

                if (!network_state.status_message.empty())
                {
                    draw_wrapped_text(font_renderer, network_state.status_message,
                                      layout.panel_rect.x, form.status_y,
                                      layout.panel_rect.w - 8, 14, muted_label);
                }

                {
                    const int footer_x = layout.footer_rect.x + 8;
                    const int footer_max_width = layout.footer_rect.w - 16;
                    int footer_y = layout.footer_rect.y + 10;
                    footer_y = draw_wrapped_text(font_renderer, "Fill the fields, then press Start.", footer_x, footer_y, footer_max_width, 14, muted_label);
                    draw_wrapped_text(font_renderer, "Port is from config.json; password is optional.", footer_x, footer_y, footer_max_width, 14, muted_label);
                }
            }
            else if (mode == ChessGuiMode::settings)
            {
                font_renderer.draw_text("Settings", layout.info_rect.x + 8, layout.info_rect.y + 42, info_size, label_color);
                font_renderer.draw_text("Click a value to edit it; yes/no fields toggle on click.", layout.info_rect.x + 8, layout.info_rect.y + 70, 13, muted_label);

                const auto field_rects = compute_settings_field_rects(layout, static_cast<int>(settings_state.fields.size()));
                for (std::size_t i = 0; i < field_rects.size(); ++i)
                {
                    const bool active = active_text_field == TextInputField::settings_value &&
                                         settings_state.selected_field_index == static_cast<int>(i);
                    fill_rect(renderer, field_rects[i], active ? button_hover : menu_item_fill);
                    draw_rect(renderer, field_rects[i], button_outline);

                    const auto &field = settings_state.fields[i];
                    const int text_size = std::min(13, field_rects[i].h - 8);
                    const int label_max_width = field_rects[i].w / 2;
                    const std::string label_text = elide_left(font_renderer, field.label, label_max_width, text_size);
                    font_renderer.draw_text(label_text, field_rects[i].x + 8, field_rects[i].y + (field_rects[i].h - text_size) / 2, text_size, label_color);

                    // Values (especially file paths) can be far wider than
                    // the row - elide from the left so the tail (the useful
                    // part of a path) stays visible instead of overflowing
                    // into the next row or past the panel edge.
                    const int label_width = font_renderer.measure_text(label_text, text_size).width;
                    const int value_max_width = std::max(20, field_rects[i].w - label_width - 24);
                    const std::string value_text = elide_left(font_renderer, field.value, value_max_width, text_size);
                    const TextMetrics value_metrics = font_renderer.measure_text(value_text, text_size);
                    font_renderer.draw_text(value_text,
                                            field_rects[i].x + field_rects[i].w - value_metrics.width - 10,
                                            field_rects[i].y + (field_rects[i].h - text_size) / 2,
                                            text_size,
                                            label_color);
                }

                {
                    const int footer_x = layout.footer_rect.x + 8;
                    const int footer_max_width = layout.footer_rect.w - 16;
                    const std::string footer_message = settings_state.status_message.empty()
                        ? "Save writes to config.json. Back discards unsaved changes."
                        : settings_state.status_message;
                    draw_wrapped_text(font_renderer, footer_message, footer_x, layout.footer_rect.y + 10, footer_max_width, 14, muted_label);
                }
            }
            else if (mode == ChessGuiMode::main_menu)
            {
                // The full game panel (turn indicator, quick-action buttons,
                // ...) only means something once a game is active; here we
                // just give the top selection bar some brief context.
                const int main_menu_max_width = layout.info_rect.w - 16;
                int main_menu_y = draw_wrapped_text(font_renderer, "Pick an option above to get started.", layout.info_rect.x + 8, layout.info_rect.y + 52, main_menu_max_width, info_size, label_color);
                const std::string tip = snapshot.move_count > 0
                    ? "A game is in progress - \"Play with current board configuration\" resumes it."
                    : "Start a new game, load one from the database, or host or join a network game.";
                draw_wrapped_text(font_renderer, tip, layout.info_rect.x + 8, main_menu_y + 6, main_menu_max_width, 15, muted_label);
            }
            else if (mode == ChessGuiMode::busy)
            {
                std::string busy_message = "Working...";
                if (!network_state.status_message.empty())
                {
                    busy_message = network_state.status_message;
                }
                else if (!database_state.status_message.empty())
                {
                    busy_message = database_state.status_message;
                }
                else if (!board_editor_state.status_message.empty())
                {
                    busy_message = board_editor_state.status_message;
                }
                font_renderer.draw_text(busy_message, layout.info_rect.x + 8, layout.info_rect.y + 52, info_size, label_color);
            }
            else // local_game or network_game: the full in-game panel
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
                if (mode == ChessGuiMode::network_game && local_player_color != playerColor::none &&
                    snapshot.current_player != playerColor::none)
                {
                    if (snapshot.current_player == local_player_color)
                    {
                        turn_line += " (You)";
                    }
                    else
                    {
                        const std::string &opponent_name = (snapshot.current_player == playerColor::white)
                            ? snapshot.white_player_name
                            : snapshot.black_player_name;
                        if (!opponent_name.empty())
                        {
                            turn_line += " (" + opponent_name + ")";
                        }
                    }
                }
                if (snapshot.current_player != playerColor::none)
                {
                    const SDL_Color indicator_color = (snapshot.current_player == playerColor::white)
                        ? make_color(245, 245, 235)
                        : make_color(30, 30, 35);
                    const SDL_Rect indicator{layout.info_rect.x + 10, layout.info_rect.y + 47, 14, 14};
                    fill_rect(renderer, indicator, indicator_color);
                    draw_rect(renderer, indicator, panel_outline);
                    font_renderer.draw_text(turn_line, layout.info_rect.x + 30, layout.info_rect.y + 44, info_size, label_color);
                }
                else
                {
                    font_renderer.draw_text(turn_line, layout.info_rect.x + 10, layout.info_rect.y + 44, info_size, label_color);
                }

                SDL_Color status_color = muted_label;
                if (snapshot.white_checkmate || snapshot.black_checkmate)
                {
                    status_color = make_color(218, 68, 83);
                }
                else if (snapshot.stalemate || snapshot.rule_draw)
                {
                    status_color = make_color(200, 160, 60);
                }
                else if (snapshot.white_checked || snapshot.black_checked)
                {
                    status_color = make_color(230, 150, 50);
                }
                font_renderer.draw_text(status_line(snapshot), layout.info_rect.x + 10, layout.info_rect.y + 70, info_size, status_color);

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

                {
                    SDL_Rect sep{layout.info_rect.x, layout.info_rect.y + layout.info_rect.h, layout.info_rect.w, 1};
                    fill_rect(renderer, sep, panel_outline);
                }

                int panel_content_y = layout.info_rect.y + layout.info_rect.h + 12;
                if (!game_action_state.message.empty())
                {
                    // Legal Moves / ML Move / Save used to only print to the
                    // console, which the GUI window has no view of - this is
                    // the same info, shown in the space below the separator
                    // that was otherwise unused during a game.
                    panel_content_y = draw_wrapped_text(font_renderer, game_action_state.message,
                                                        layout.info_rect.x + 10, panel_content_y,
                                                        layout.info_rect.w - 20, 14, muted_label);
                }

                if (mode == ChessGuiMode::network_game)
                {
                    // The input box is anchored above the footer (fixed
                    // height, see compute_chat_input_rect) so it never moves
                    // regardless of how much history or game_action_state
                    // text is shown above it; the history area fills
                    // whatever is left between the two.
                    const SDL_Rect chat_input_rect = compute_chat_input_rect(layout);

                    font_renderer.draw_text("Chat", layout.info_rect.x + 10, panel_content_y + 6, 13, muted_label);
                    int history_y = panel_content_y + 24;
                    const int history_bottom = chat_input_rect.y - 8;

                    constexpr std::size_t max_shown = 8;
                    const std::size_t total = chat_state.messages.size();
                    const std::size_t start = (total > max_shown) ? (total - max_shown) : 0;
                    for (std::size_t i = start; i < total && history_y < history_bottom; ++i)
                    {
                        history_y = draw_wrapped_text(font_renderer, chat_state.messages[i],
                                                      layout.info_rect.x + 10, history_y,
                                                      layout.info_rect.w - 20, 13, muted_label, 2);
                    }

                    fill_rect(renderer, chat_input_rect, active_text_field == TextInputField::chat_message ? button_hover : menu_item_fill);
                    draw_rect(renderer, chat_input_rect, button_outline);
                    const bool show_placeholder = chat_state.pending_input.empty();
                    const std::string chat_display = show_placeholder ? "Type a message, Enter to send..." : chat_state.pending_input;
                    const std::string chat_shown = elide_left(font_renderer, chat_display, chat_input_rect.w - 16, 14);
                    font_renderer.draw_text(chat_shown, chat_input_rect.x + 8, chat_input_rect.y + 7, 14, show_placeholder ? muted_label : label_color);
                }

                const std::string mode_line = (mode == ChessGuiMode::network_game)
                    ? "Network game active"
                    : "Drag pieces or use the buttons above";
                font_renderer.draw_text(mode_line, layout.footer_rect.x + 8, layout.footer_rect.y + 10, 15, muted_label);
                font_renderer.draw_text("Moves played: " + std::to_string(snapshot.move_count), layout.footer_rect.x + 8, layout.footer_rect.y + 28, 15, muted_label);
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
        ChessGuiSettingsState settings_state_{};
        ChessGuiGameActionState game_action_state_{};
        ChessGuiChatState chat_state_{};
        playerColor local_player_color_ = playerColor::none;
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
