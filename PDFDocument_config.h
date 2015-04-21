#ifndef PDFDOCUMENT_CONFIG_H
#define PDFDOCUMENT_CONFIG_H

#include <QByteArray>
struct PDFDocumentConfig{
	bool autoHideToolbars;
	int windowLeft, windowTop;
	int windowWidth, windowHeight;
	bool windowMaximized;
	QByteArray windowState;

	int cacheSizeMB;
	int loadStrategy;

	int dpi;
	int scaleOption;
	int scale;

	double zoomStepFactor;

	int magnifierSize, magnifierShape;
	bool magnifierBorder;

	QString syncFileMask;
	QString highlightColor; // hex rgba, e.g. #FFFF003F
	int highlightDuration;

//live options
	int editTool;
	bool continuous,singlepagestep;
	bool followFromCursor, followFromScroll, syncViews;
	bool invertColors;
    bool grayscale;
	int gridx,gridy;
};

#endif // PDFDOCUMENT_CONFIG_H
