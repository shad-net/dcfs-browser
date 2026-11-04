// SPDX-FileCopyrightText: Copyright 2026 dcfs-browser Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "archive_scanner.h"
#include "evo_file_system.h"
#include "evo_reader.h"
#include "file_list.h"
#include "main_window.h"
#include "resource_export.h"
#include "texture_batch.h"
#include "version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QTextStream>

#include <cstring>
#include <memory>

namespace {

// --list / --extract run without a window.
bool wantsConsole(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        for (const char* flag : {"-l", "--list", "-x", "--extract", "-s", "--scan", "-e",
                                 "--export", "-t", "--export-textures"}) {
            if (std::strcmp(argv[i], flag) == 0) {
                return true;
            }
        }
    }
    return false;
}

// --export: the file names of an archive (or of one gameNNN.dat) to a list, or one list per
// data file into a folder.
int runExport(const QString& path, const QString& target, const QString& formatName,
              bool perDataFile, const QString& only) {
    QTextStream out(stdout);
    QTextStream err(stderr);
    const auto format =
        fileListFormatFromName(formatName.isEmpty() ? QStringLiteral("names") : formatName);
    if (!format) {
        err << "[x] Unknown format \"" << formatName << "\" (use names, detailed or csv)."
            << Qt::endl;
        return 1;
    }
    EvoFileSystem fs;
    QString error;
    if (!fs.open(path, &error)) {
        err << "[x] " << error << Qt::endl;
        return 1;
    }
    if (perDataFile) {
        const int written = writeFileListPerDataFile(fs, target, *format, &error);
        if (written < 0) {
            err << "[x] " << error << Qt::endl;
            return 1;
        }
        out << "[/] Wrote " << written << " file lists to " << QDir::toNativeSeparators(target)
            << Qt::endl;
        return 0;
    }
    std::vector<int> entries;
    for (int i : entriesInDataFile(fs, fs.focusDataIndex())) {
        if (only.isEmpty() ||
            fs.index().entries[size_t(i)].name.contains(only, Qt::CaseInsensitive)) {
            entries.push_back(i);
        }
    }
    const size_t count = entries.size();
    if (!writeFileList(fs, std::move(entries), target, *format, &error)) {
        err << "[x] " << error << Qt::endl;
        return 1;
    }
    out << "[/] Wrote " << count << " names to " << QDir::toNativeSeparators(target) << Qt::endl;
    return 0;
}

// --list / --extract on a resource pack: a .rpk on disk, or one inside an archive (--rpk).
// --export-textures: the textures of every resource pack in the archive (or one data file).
int runTextureExport(const QString& path, const QString& outputDir, const QString& formatName,
                     bool everything, const QString& only) {
    QTextStream out(stdout);
    QTextStream err(stderr);
    ResourceExportOptions options;
    const QString format = formatName.isEmpty() ? QStringLiteral("dds") : formatName.toLower();
    if (format != QLatin1String("dds") && format != QLatin1String("png") &&
        format != QLatin1String("both")) {
        err << "[x] Unknown texture format \"" << formatName << "\" (use dds, png or both)."
            << Qt::endl;
        return 1;
    }
    options.dds = format != QLatin1String("png");
    options.png = format != QLatin1String("dds");
    options.texturesOnly = !everything;
    options.other = everything;

    auto fs = std::make_shared<EvoFileSystem>();
    QString error;
    if (!fs->open(path, &error)) {
        err << "[x] " << error << Qt::endl;
        return 1;
    }
    std::vector<int> entries;
    for (int i : entriesInDataFile(*fs, fs->focusDataIndex())) {
        if (only.isEmpty() ||
            fs->index().entries[size_t(i)].name.contains(only, Qt::CaseInsensitive)) {
            entries.push_back(i);
        }
    }
    const std::vector<int> packs = TextureBatch::packsIn(*fs, entries);
    out << "[:] " << packs.size() << " resource packs" << Qt::endl;
    TextureBatch batch(fs, outputDir, options);
    QStringList errors;
    for (int entry : packs) {
        batch.exportPack(entry, &errors);
    }
    for (const QString& line : errors) {
        err << "[x] " << line << Qt::endl;
    }
    const TextureBatchStats& stats = batch.stats();
    out << "[/] Wrote " << stats.textures << " textures from " << stats.packs << " packs to "
        << QDir::toNativeSeparators(outputDir);
    if (stats.resources > 0) {
        out << " (+" << stats.resources << " other resources)";
    }
    if (stats.noTextures > 0) {
        out << ", " << stats.noTextures << " packs without textures";
    }
    out << Qt::endl;
    return errors.isEmpty() ? 0 : 2;
}

int runPack(const QString& path, const QString& rpkName, bool list, const QString& outputDir,
            const ResourceExportOptions& options) {
    QTextStream out(stdout);
    QTextStream err(stderr);
    QString error;
    std::unique_ptr<PackContext> context;
    if (rpkName.isEmpty()) {
        context = openPackFromDisk(path, &error);
    } else {
        auto fs = std::make_shared<EvoFileSystem>();
        if (fs->open(path, &error)) {
            const int index = fs->index().find(rpkName);
            if (index < 0) {
                error = QStringLiteral("%1 is not in the archive.").arg(rpkName);
            } else {
                context = openPackFromArchive(fs, index, &error);
            }
        }
    }
    if (!context) {
        err << "[x] " << error << Qt::endl;
        return 1;
    }
    const ResourcePack& pack = context->pack();
    out << "[:] " << context->path() << ": " << pack.resources().size() << " resources, version 0x"
        << QString::number(pack.version(), 16) << Qt::endl;
    if (list) {
        for (const ResourceInfo& info : pack.resources()) {
            out << resourceTypeName(info.type()).leftJustified(20) << ' ' << info.displayName()
                << " (" << info.size << " bytes)\n";
        }
        out.flush();
    }
    if (outputDir.isEmpty()) {
        return 0;
    }
    QSet<QString> used;
    int written = 0;
    int skipped = 0;
    int failed = 0;
    for (const ResourceInfo& info : pack.resources()) {
        bool wasSkipped = false;
        if (!context->exportResource(info, outputDir, options, &used, &wasSkipped, &error)) {
            err << "[x] " << info.displayName() << ": " << error << Qt::endl;
            ++failed;
        } else if (wasSkipped) {
            ++skipped;
        } else {
            ++written;
        }
    }
    out << "[/] Exported " << written << " resources to " << QDir::toNativeSeparators(outputDir)
        << " (" << skipped << " skipped, " << failed << " failed)" << Qt::endl;
    return failed == 0 ? 0 : 2;
}

int runConsole(const QString& path, bool list, const QString& outputDir, bool verify,
               const QString& only) {
    QTextStream out(stdout);
    QTextStream err(stderr);
    auto fs = std::make_shared<EvoFileSystem>();
    QString error;
    if (!fs->open(path, &error)) {
        err << "[x] " << error << Qt::endl;
        return 1;
    }
    const EvoIndex& index = fs->index();
    out << "[:] " << QDir::toNativeSeparators(fs->indexPath()) << ": version " << index.version
        << ", " << compressionName(index.compression) << ", " << index.entries.size() << " entries"
        << Qt::endl;

    std::vector<int> selected;
    for (int i = 0; i < int(index.entries.size()); ++i) {
        const EvoIndexEntry& e = index.entries[size_t(i)];
        if (fs->focusDataIndex() >= 0 && e.datIndex != fs->focusDataIndex()) {
            continue;
        }
        if (!only.isEmpty() && !e.name.contains(only, Qt::CaseInsensitive)) {
            continue;
        }
        selected.push_back(i);
    }

    if (list) {
        for (int i : selected) {
            const EvoIndexEntry& e = index.entries[size_t(i)];
            out << e.name << " (" << e.size << " bytes, " << fs->dataFileName(e.datIndex)
                << ", offset 0x" << QString::number(e.offset, 16).toUpper() << ")\n";
        }
        out.flush();
    }
    if (outputDir.isEmpty()) {
        return 0;
    }

    EvoReader reader(fs);
    int failed = 0;
    for (int i : selected) {
        const EvoIndexEntry& e = index.entries[size_t(i)];
        if (!reader.extractTo(e, outputDir, verify, &error)) {
            err << "[x] " << e.name << ": " << error << Qt::endl;
            ++failed;
        }
    }
    out << "[/] Extracted " << (selected.size() - size_t(failed)) << " of " << selected.size()
        << " files to " << QDir::toNativeSeparators(outputDir) << Qt::endl;
    return failed == 0 ? 0 : 2;
}

// Prints every archive below `root`, like the Archives panel.
int runScan(QCoreApplication& app, const QString& root) {
    QTextStream out(stdout);
    ArchiveScanner scanner(root);
    QObject::connect(&scanner, &ArchiveScanner::found, &app,
                     [&out](const std::shared_ptr<ScannedArchive>& archive) {
                         const EvoFileSystem& fs = *archive->fs;
                         out << QDir::toNativeSeparators(fs.indexPath()) << ": version "
                             << fs.index().version << ", " << fs.index().entries.size() << " files";
                         if (archive->missing > 0) {
                             out << ", " << archive->missing << " data files missing";
                         }
                         out << "\n";
                         for (const ScannedArchive::DataFile& data : archive->dataFiles) {
                             if (!fs.isAlpha()) {
                                 out << "    " << data.name << ": " << data.files << " files"
                                     << (data.present
                                             ? QString()
                                             : QStringLiteral(" (") + data.error + QLatin1Char(')'))
                                     << "\n";
                             }
                         }
                         out.flush();
                     });
    int result = 0;
    QObject::connect(&scanner, &ArchiveScanner::finished, &app,
                     [&](int archives, int orphans, const QStringList& failed, bool) {
                         out << "[/] " << archives << " archives";
                         if (orphans > 0) {
                             out << ", " << orphans << " data files without a game.ndx skipped";
                         }
                         out << Qt::endl;
                         for (const QString& line : failed) {
                             QTextStream(stderr) << "[x] " << line << Qt::endl;
                         }
                         result = archives > 0 ? 0 : 1;
                         app.quit();
                     });
    scanner.start();
    app.exec();
    return result;
}

} // namespace

int main(int argc, char* argv[]) {
    std::unique_ptr<QCoreApplication> app;
    if (wantsConsole(argc, argv)) {
        app = std::make_unique<QCoreApplication>(argc, argv);
    } else {
        app = std::make_unique<QApplication>(argc, argv);
    }
    QCoreApplication::setOrganizationName(QStringLiteral("driveclub-server"));
    QCoreApplication::setApplicationName(QStringLiteral("dcfs-browser"));
    QCoreApplication::setApplicationVersion(
        QStringLiteral("%1 (built %2)").arg(Driveclub::Version(), Driveclub::BuildTimestamp()));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Browse and extract Driveclub's game.ndx / gameNNN.dat archives."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("path"), QStringLiteral("game.ndx, game.dat, gameNNN.dat or their folder."));
    const QCommandLineOption listOption({"l", "list"}, "Print the files and exit.");
    const QCommandLineOption extractOption({"x", "extract"}, "Extract the files to <dir> and exit.",
                                           "dir");
    const QCommandLineOption filterOption({"f", "filter"}, "Only files whose path contains <text>.",
                                          "text");
    const QCommandLineOption noVerifyOption("no-verify", "Do not check MD5 checksums.");
    const QCommandLineOption scanOption(
        {"s", "scan"}, "List every archive in <dir> and its subfolders, then exit.", "dir");
    const QCommandLineOption exportOption(
        {"e", "export"}, "Write the file names to <file> (or, with --per-data-file, <folder>).",
        "file");
    const QCommandLineOption formatOption(
        "format", "Format for --export: names (default), detailed or csv.", "format");
    const QCommandLineOption perDataOption("per-data-file",
                                           "With --export: one list per gameNNN.dat.");
    const QCommandLineOption rpkOption(
        "rpk", "With --list/--extract: work on the resource pack <name> inside the archive.",
        "name");
    const QCommandLineOption pngOption("png", "With a resource pack: also save textures as PNG.");
    const QCommandLineOption rawOption(
        "raw-other", "With a resource pack: also save other resource types as raw data.");
    const QCommandLineOption texturesOption(
        {"t", "export-textures"},
        "Write the textures inside every resource pack (filter with --filter) to <dir>.", "dir");
    const QCommandLineOption textureFormatOption(
        "texture-format", "For --export-textures: dds (default), png or both.", "format");
    const QCommandLineOption everythingOption(
        "all-resources", "With --export-textures: also XML, BIN and other resources.");
    parser.addOptions({listOption, extractOption, filterOption, noVerifyOption, scanOption,
                       exportOption, formatOption, perDataOption, rpkOption, pngOption, rawOption,
                       texturesOption, textureFormatOption, everythingOption});
    parser.process(*app);

    const QStringList positional = parser.positionalArguments();
    if (parser.isSet(scanOption)) {
        return runScan(*app, parser.value(scanOption));
    }
    if (parser.isSet(texturesOption)) {
        if (positional.isEmpty()) {
            QTextStream(stderr) << "A path is required with --export-textures.\n";
            return 1;
        }
        return runTextureExport(positional.first(), parser.value(texturesOption),
                                parser.value(textureFormatOption), parser.isSet(everythingOption),
                                parser.value(filterOption));
    }
    if (parser.isSet(exportOption)) {
        if (positional.isEmpty()) {
            QTextStream(stderr) << "A path is required with --export.\n";
            return 1;
        }
        return runExport(positional.first(), parser.value(exportOption), parser.value(formatOption),
                         parser.isSet(perDataOption), parser.value(filterOption));
    }
    const bool packPath = !positional.isEmpty() &&
                          positional.first().endsWith(QLatin1String(".rpk"), Qt::CaseInsensitive);
    if ((parser.isSet(listOption) || parser.isSet(extractOption)) &&
        (parser.isSet(rpkOption) || packPath)) {
        if (positional.isEmpty()) {
            QTextStream(stderr) << "A path is required.\n";
            return 1;
        }
        ResourceExportOptions options;
        options.png = parser.isSet(pngOption);
        options.other = parser.isSet(rawOption);
        return runPack(positional.first(), parser.value(rpkOption), parser.isSet(listOption),
                       parser.value(extractOption), options);
    }
    if (parser.isSet(listOption) || parser.isSet(extractOption)) {
        if (positional.isEmpty()) {
            QTextStream(stderr) << "A path is required with --list or --extract.\n";
            return 1;
        }
        return runConsole(positional.first(), parser.isSet(listOption), parser.value(extractOption),
                          !parser.isSet(noVerifyOption), parser.value(filterOption));
    }

    MainWindow window;
    window.show();
    if (!positional.isEmpty()) {
        window.openPath(positional.first());
    }
    return app->exec();
}
