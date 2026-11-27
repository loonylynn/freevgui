// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Alibek Omarov

#include "platform/common/font.h"

#ifdef VGUI_HAVE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H

#include <limits.h>
#include <string.h>

namespace
{

static int FontRound26_6( FT_Pos value )
{
	if( value >= 0 )
		return (int)(( value + 32 ) >> 6 );

	return -(int)(( -value + 32 ) >> 6 );
}

class FontPlat_FreeType : public vgui::BaseFontPlat
{
public:
	FontPlat_FreeType() :
		m_name( nullptr ),
		m_fontData( nullptr ),
		m_library( nullptr ),
		m_face( nullptr ),
		m_tall( 0 ),
		m_wide( 0 ),
		m_ascender( 0 ),
		m_weight( 0 ),
		m_italic( false ),
		m_underline( false ),
		m_strikeout( false ),
		m_symbol( false )
	{
	}

	virtual ~FontPlat_FreeType() override
	{
		if( m_face )
			FT_Done_Face( m_face );

		if( m_library )
			FT_Done_FreeType( m_library );

		delete[] m_fontData;
		delete[] m_name;
	}

	static FontPlat_FreeType *Create( const char *name, const void *data, int dataLength,
		int tall, int wide, float rotation, int weight,
		bool italic, bool underline, bool strikeout, bool symbol )
	{
		FontPlat_FreeType *font = new FontPlat_FreeType();

		if( !font )
			return nullptr;

		if( !font->init( name, data, dataLength, tall, wide, rotation,
			weight, italic, underline, strikeout, symbol ))
		{
			delete font;
			return nullptr;
		}

		return font;
	}

	virtual bool equals( const char *name, int tall, int wide, float rotation,
		int weight, bool italic, bool underline, bool strikeout, bool symbol ) override
	{
		(void)wide;
		(void)rotation;

		return name && m_name && !strcmp( m_name, name )
			&& m_tall == tall && m_weight == weight
			&& m_italic == italic && m_underline == underline
			&& m_strikeout == strikeout && m_symbol == symbol;
	}

	virtual void getCharRGBA( int ch, int rgbaX, int rgbaY,
		int rgbaWide, int rgbaTall, unsigned char *rgba ) override
	{
		if( !rgba || !m_face || FT_Load_Char( m_face, (FT_ULong)(unsigned char)ch,
			FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL ))
			return;

		const FT_Bitmap &bitmap = m_face->glyph->bitmap;
		const int top = m_ascender - m_face->glyph->bitmap_top;
		const int pitch = bitmap.pitch;
		const int absPitch = pitch >= 0 ? pitch : -pitch;

		for( unsigned int y = 0; y < bitmap.rows; y++ )
		{
			const int outY = rgbaY + top + (int)y;
			if( outY < 0 || outY >= rgbaTall )
				continue;

			const unsigned char *row = pitch >= 0
				? bitmap.buffer + y * absPitch
				: bitmap.buffer + ( bitmap.rows - 1 - y ) * absPitch;

			for( unsigned int x = 0; x < bitmap.width; x++ )
			{
				const int outX = rgbaX + (int)x;
				if( outX < 0 || outX >= rgbaWide )
					continue;

				unsigned char coverage = 0;

				switch( bitmap.pixel_mode )
				{
				case FT_PIXEL_MODE_GRAY:
					if( bitmap.num_grays > 1 )
						coverage = (unsigned char)( row[x] * 255 / ( bitmap.num_grays - 1 ));
					break;

				case FT_PIXEL_MODE_MONO:
					coverage = ( row[x >> 3] & ( 0x80 >> ( x & 7 ))) ? 255 : 0;
					break;

				case FT_PIXEL_MODE_BGRA:
					coverage = row[x * 4 + 3];
					break;

				case FT_PIXEL_MODE_LCD:
					coverage = (unsigned char)(( row[x * 3] + row[x * 3 + 1] + row[x * 3 + 2] ) / 3);
					break;

				default:
					break;
				}

				unsigned char *dst = &rgba[( outY * rgbaWide + outX ) * 4];
				dst[0] = dst[1] = dst[2] = 255;
				dst[3] = coverage;
			}
		}
	}

	virtual void getCharABCwide( int ch, int &a, int &b, int &c ) override
	{
		a = b = c = 0;

		if( !m_face || FT_Load_Char( m_face, (FT_ULong)(unsigned char)ch,
			FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL ))
			return;

		const FT_GlyphSlot glyph = m_face->glyph;
		const int advance = FontRound26_6( glyph->advance.x );

		// Use the rendered bitmap's bearings and width so the layout metrics
		// exactly match the glyph rectangle uploaded to the VGUI texture atlas.
		a = glyph->bitmap_left;
		b = (int)glyph->bitmap.width;
		c = advance - a - b;
	}

	virtual int getTall() override
	{
		return m_tall;
	}

	virtual int getWide() override
	{
		return m_wide;
	}

	virtual void drawSetTextFont( vgui::SurfacePlat * ) override
	{
		// XashSurface owns glyph rasterization and atlas caching.
	}

private:
	bool init( const char *name, const void *data, int dataLength,
		int tall, int wide, float rotation, int weight,
		bool italic, bool underline, bool strikeout, bool symbol )
	{
		(void)rotation;

		if( !name || !data || dataLength <= 0 || dataLength > LONG_MAX )
			return false;

		const size_t nameLength = strlen( name );
		m_name = new char[nameLength + 1];
		if( !m_name )
			return false;
		memcpy( m_name, name, nameLength + 1 );

		m_fontData = new FT_Byte[dataLength];
		if( !m_fontData )
			return false;
		memcpy( m_fontData, data, dataLength );

		m_weight = weight;
		m_italic = italic;
		m_underline = underline;
		m_strikeout = strikeout;
		m_symbol = symbol;

		if( FT_Init_FreeType( &m_library ))
			return false;

		if( FT_New_Memory_Face( m_library, m_fontData, (FT_Long)dataLength, 0, &m_face ))
			return false;

		// Prefer Unicode when the font has a Unicode charmap. Some legacy
		// fonts expose only a platform-specific charmap, which FreeType can
		// still use as its default if selection fails.
		FT_Select_Charmap( m_face, FT_ENCODING_UNICODE );

		if( tall <= 0 )
			tall = 16;

		if( FT_Set_Pixel_Sizes( m_face, (FT_UInt)( wide > 0 ? wide : 0 ), (FT_UInt)tall ))
			return false;

		m_ascender = FontRound26_6( m_face->size->metrics.ascender );
		m_tall = FontRound26_6( m_face->size->metrics.height );
		m_wide = FontRound26_6( m_face->size->metrics.max_advance );

		if( m_tall < 1 )
			m_tall = tall;
		if( m_wide < 1 )
			m_wide = tall;

		return true;
	}

	char *m_name;
	FT_Byte *m_fontData;
	FT_Library m_library;
	FT_Face m_face;
	int m_tall;
	int m_wide;
	int m_ascender;
	int m_weight;
	bool m_italic;
	bool m_underline;
	bool m_strikeout;
	bool m_symbol;
};

} // namespace

#endif // VGUI_HAVE_FREETYPE

using namespace vgui;

BaseFontPlat *vgui::FontPlat_CreateSystem( const char *, int, int, float, int, bool, bool, bool, bool )
{
	// POSIX system-font lookup is deliberately separate from loading a
	// specific font file supplied by the game through the VGUI Font API.
	return nullptr;
}

BaseFontPlat *vgui::FontPlat_CreateFromMemory( const char *name, const void *data, int dataLength,
	int tall, int wide, float rotation, int weight, bool italic, bool underline,
	bool strikeout, bool symbol )
{
#ifdef VGUI_HAVE_FREETYPE
	return FontPlat_FreeType::Create( name, data, dataLength, tall, wide, rotation,
		weight, italic, underline, strikeout, symbol );
#else
	(void)name;
	(void)data;
	(void)dataLength;
	(void)tall;
	(void)wide;
	(void)rotation;
	(void)weight;
	(void)italic;
	(void)underline;
	(void)strikeout;
	(void)symbol;
	return nullptr;
#endif
}
