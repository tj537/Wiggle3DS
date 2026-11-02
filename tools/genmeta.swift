// Renders the HOME menu icon (48x48), the banner (256x128) and a short, quiet banner chime
// for the CIA build with CoreGraphics/CoreText.
// usage: swift tools/genmeta.swift <out dir>   (writes icon.png, banner.png, banner.wav)
import CoreGraphics
import CoreText
import Foundation
import ImageIO
import UniformTypeIdentifiers

let outDir = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "meta"
let fontDir = URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent()
	.deletingLastPathComponent().appendingPathComponent("fonts")

func rgb(_ hex: UInt32, _ a: CGFloat = 1) -> CGColor {
	CGColor(srgbRed: CGFloat(hex >> 16 & 0xFF) / 255, green: CGFloat(hex >> 8 & 0xFF) / 255,
	        blue: CGFloat(hex & 0xFF) / 255, alpha: a)
}

let bg = rgb(0x140B24), violet = rgb(0x7C4DFF), pink = rgb(0xD66EFF)
let accent = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: [violet, pink] as CFArray, locations: [0, 1])!

func font(_ file: String, _ size: CGFloat) -> CTFont {
	let data = try! Data(contentsOf: fontDir.appendingPathComponent(file))
	return CTFontCreateWithGraphicsFont(CGFont(CGDataProvider(data: data as CFData)!)!, size, nil, nil)
}

func canvas(_ w: Int, _ h: Int) -> CGContext {
	let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0,
	                    space: CGColorSpace(name: CGColorSpace.sRGB)!,
	                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
	ctx.setShouldAntialias(true)
	ctx.interpolationQuality = .high
	return ctx
}

func save(_ ctx: CGContext, _ name: String) {
	let url = URL(fileURLWithPath: outDir).appendingPathComponent(name)
	let dest = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil)!
	CGImageDestinationAddImage(dest, ctx.makeImage()!, nil)
	CGImageDestinationFinalize(dest)
}

// Soft radial glow in the accent colour.
func glow(_ ctx: CGContext, _ c: CGPoint, _ r: CGFloat, _ color: UInt32, _ a: CGFloat) {
	let g = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB),
	                   colors: [rgb(color, a), rgb(color, 0)] as CFArray, locations: [0, 1])!
	ctx.drawRadialGradient(g, startCenter: c, startRadius: 0, endCenter: c, endRadius: r, options: [])
}

// Glyph outlines of a string as one path, with the baseline at y = 0.
func textPath(_ s: String, _ f: CTFont, kern: CGFloat = 0) -> CGPath {
	let attr = NSAttributedString(string: s, attributes: [
		NSAttributedString.Key(kCTFontAttributeName as String): f,
		NSAttributedString.Key(kCTKernAttributeName as String): kern,
	])
	let line = CTLineCreateWithAttributedString(attr)
	let path = CGMutablePath()
	for run in CTLineGetGlyphRuns(line) as! [CTRun] {
		let n = CTRunGetGlyphCount(run)
		var glyphs = [CGGlyph](repeating: 0, count: n), pos = [CGPoint](repeating: .zero, count: n)
		CTRunGetGlyphs(run, CFRange(), &glyphs)
		CTRunGetPositions(run, CFRange(), &pos)
		for i in 0..<n {
			if let g = CTFontCreatePathForGlyph(f, glyphs[i], nil) {
				path.addPath(g, transform: CGAffineTransform(translationX: pos[i].x, y: pos[i].y))
			}
		}
	}
	return path
}

// Fills a path with the violet → pink gradient running left to right across its bounds.
func fillGradient(_ ctx: CGContext, _ p: CGPath) {
	let b = p.boundingBoxOfPath
	ctx.saveGState()
	ctx.addPath(p)
	ctx.clip()
	ctx.drawLinearGradient(accent, start: CGPoint(x: b.minX, y: b.maxY), end: CGPoint(x: b.maxX, y: b.minY), options: [])
	ctx.restoreGState()
}

// The mark: a gradient "W" with a violet "ghost" copy offset to the left, like the other eye of a
// wigglegram. `size` is the font size, `at` the glyph box centre.
func mark(_ ctx: CGContext, _ size: CGFloat, at c: CGPoint, ghost: CGFloat) {
	let p = textPath("W", font("Inter-Bold.ttf", size))
	let b = p.boundingBoxOfPath
	let t = CGAffineTransform(translationX: c.x - b.midX, y: c.y - b.midY)
	let ghostPath = p.copy(using: [t.translatedBy(x: -ghost, y: 0)])!
	ctx.addPath(ghostPath)
	ctx.setFillColor(rgb(0x7C4DFF, 0.42))
	ctx.fillPath()
	fillGradient(ctx, p.copy(using: [t.translatedBy(x: ghost * 0.35, y: 0)])!)
}

func roundedRect(_ r: CGRect, _ radius: CGFloat) -> CGPath {
	CGPath(roundedRect: r, cornerWidth: radius, cornerHeight: radius, transform: nil)
}

try? FileManager.default.createDirectory(atPath: outDir, withIntermediateDirectories: true)

// --- Icon, 48x48 -------------------------------------------------------------------------------
do {
	let ctx = canvas(48, 48)
	ctx.setFillColor(bg)
	ctx.fill(CGRect(x: 0, y: 0, width: 48, height: 48))
	glow(ctx, CGPoint(x: 30, y: 16), 34, 0xD66EFF, 0.22)
	glow(ctx, CGPoint(x: 14, y: 34), 30, 0x7C4DFF, 0.28)
	// thin gradient frame, like a photo border
	let frame = roundedRect(CGRect(x: 3.5, y: 3.5, width: 41, height: 41), 8)
	ctx.saveGState()
	ctx.addPath(frame)
	ctx.setLineWidth(1.5)
	ctx.replacePathWithStrokedPath()
	ctx.clip()
	ctx.drawLinearGradient(accent, start: CGPoint(x: 0, y: 48), end: CGPoint(x: 48, y: 0), options: [])
	ctx.restoreGState()
	mark(ctx, 34, at: CGPoint(x: 24, y: 24), ghost: 3.5)
	save(ctx, "icon.png")
}

// --- Banner, 256x128 ---------------------------------------------------------------------------
// The HOME menu shows the banner on a 3D plane; keep the content inside the middle area.
do {
	let w: CGFloat = 256, h: CGFloat = 128
	let ctx = canvas(Int(w), Int(h))
	ctx.setFillColor(bg)
	ctx.fill(CGRect(x: 0, y: 0, width: w, height: h))
	glow(ctx, CGPoint(x: 40, y: 92), 120, 0x7C4DFF, 0.32)
	glow(ctx, CGPoint(x: 226, y: 22), 110, 0xD66EFF, 0.22)

	// card behind the content
	let card = roundedRect(CGRect(x: 10, y: 14, width: w - 20, height: h - 28), 16)
	ctx.addPath(card)
	ctx.setFillColor(rgb(0xFFFFFF, 0.035))
	ctx.fillPath()
	ctx.saveGState()
	ctx.addPath(card)
	ctx.setLineWidth(1.5)
	ctx.replacePathWithStrokedPath()
	ctx.clip()
	ctx.setAlpha(0.7)
	ctx.drawLinearGradient(accent, start: CGPoint(x: 10, y: h), end: CGPoint(x: w - 10, y: 0), options: [])
	ctx.restoreGState()

	// layout: icon tile, gap, text block, centred as a group
	let titleFont = font("Inter-Bold.ttf", 25), subFont = font("Inter-Medium.ttf", 10)
	let wiggle = textPath("Wiggle", titleFont, kern: -0.3), three = textPath("3DS", titleFont, kern: -0.2)
	let sub = textPath("Stereo photos → wigglegrams", subFont)
	let wiggleW = wiggle.boundingBoxOfPath.maxX + 1.5
	let textW = max(wiggleW + three.boundingBoxOfPath.maxX, sub.boundingBoxOfPath.maxX)
	let tileS: CGFloat = 50, gap: CGFloat = 13
	let left = ((w - (tileS + gap + textW)) / 2).rounded()

	// icon tile
	let tile = CGRect(x: left, y: (h - tileS) / 2, width: tileS, height: tileS)
	ctx.addPath(roundedRect(tile, 12))
	ctx.setFillColor(rgb(0x140B24))
	ctx.fillPath()
	ctx.saveGState()
	ctx.addPath(roundedRect(tile, 12))
	ctx.clip()
	glow(ctx, CGPoint(x: tile.midX - 10, y: tile.midY + 10), 50, 0x7C4DFF, 0.35)
	ctx.restoreGState()
	ctx.saveGState()
	ctx.addPath(roundedRect(tile.insetBy(dx: 0.75, dy: 0.75), 11.5))
	ctx.setLineWidth(1.5)
	ctx.replacePathWithStrokedPath()
	ctx.clip()
	ctx.drawLinearGradient(accent, start: CGPoint(x: tile.minX, y: tile.maxY), end: CGPoint(x: tile.maxX, y: tile.minY), options: [])
	ctx.restoreGState()
	mark(ctx, 34, at: CGPoint(x: tile.midX, y: tile.midY), ghost: 3.5)

	// title: "Wiggle" in white, "3DS" in the gradient; subtitle below
	let x0 = tile.maxX + gap, base = tile.midY + 1
	ctx.addPath(wiggle.copy(using: [CGAffineTransform(translationX: x0, y: base)])!)
	ctx.setFillColor(rgb(0xF4EEFF))
	ctx.fillPath()
	fillGradient(ctx, three.copy(using: [CGAffineTransform(translationX: x0 + wiggleW, y: base)])!)
	ctx.addPath(sub.copy(using: [CGAffineTransform(translationX: x0 + 1, y: base - 17)])!)
	ctx.setFillColor(rgb(0xB9A6E6))
	ctx.fillPath()
	save(ctx, "banner.png")
}

// --- Banner chime: two soft bell tones, quiet, ~0.9 s, 16-bit mono 22050 Hz ----------------------
do {
	let rate = 22050, n = rate * 9 / 10
	var samples = [Int16](repeating: 0, count: n)
	let notes: [(t: Double, f: Double)] = [(0.0, 783.99), (0.16, 1174.66)] // G5, D6
	for i in 0..<n {
		let t = Double(i) / Double(rate)
		var v = 0.0
		for note in notes where t >= note.t {
			let d = t - note.t
			let env = min(1, d / 0.006) * exp(-d * 5.5)
			v += env * (sin(2 * .pi * note.f * d) + 0.25 * sin(2 * .pi * note.f * 2 * d))
		}
		let fade = min(1, Double(n - i) / Double(rate / 20))
		samples[i] = Int16(max(-1, min(1, v * 0.16 * fade)) * 32767)
	}
	var wav = Data()
	func u32(_ v: Int) { var x = UInt32(v).littleEndian; wav.append(Data(bytes: &x, count: 4)) }
	func u16(_ v: Int) { var x = UInt16(v).littleEndian; wav.append(Data(bytes: &x, count: 2)) }
	wav.append("RIFF".data(using: .ascii)!); u32(36 + n * 2)
	wav.append("WAVEfmt ".data(using: .ascii)!); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16)
	wav.append("data".data(using: .ascii)!); u32(n * 2)
	samples.withUnsafeBytes { wav.append(contentsOf: $0) }
	try! wav.write(to: URL(fileURLWithPath: outDir).appendingPathComponent("banner.wav"))
}
