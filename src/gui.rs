use eframe::egui;
use egui_extras::{Column, TableBuilder};
use crate::core::*;
use std::sync::Arc;
use std::time::Instant;
use std::thread;
use parking_lot::Mutex;

pub struct FSearchApp {
    query: String,
    results: Vec<Arc<Entry>>,
    store: Arc<Mutex<Option<EntryStore>>>,
    
    match_case: bool,
    use_regex: bool,
    match_path: bool,
    
    search_time: f32,
    initialized: bool,
    is_scanning: bool,
}

impl FSearchApp {
    pub fn new(path: std::path::PathBuf) -> Self {
        let store = Arc::new(Mutex::new(None));
        let store_clone = Arc::clone(&store);
        
        // 在后台线程启动扫描，不阻塞 GUI 窗口弹出
        thread::spawn(move || {
            let mut scanner = Scanner::new();
            if let Ok(new_store) = scanner.scan(path) {
                *store_clone.lock() = Some(new_store);
            }
        });

        Self {
            query: String::new(),
            results: Vec::new(),
            store,
            match_case: false,
            use_regex: false,
            match_path: false,
            search_time: 0.0,
            initialized: false,
            is_scanning: true,
        }
    }

    fn setup_fonts(&mut self, ctx: &egui::Context) {
        let mut fonts = egui::FontDefinitions::default();
        let chinese_font_paths = [
            "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        ];

        for path in chinese_font_paths {
            if let Ok(font_data) = std::fs::read(path) {
                fonts.font_data.insert("chinese".to_owned(), egui::FontData::from_owned(font_data));
                fonts.families.get_mut(&egui::FontFamily::Proportional).unwrap().insert(0, "chinese".to_owned());
                fonts.families.get_mut(&egui::FontFamily::Monospace).unwrap().push("chinese".to_owned());
                break;
            }
        }
        ctx.set_fonts(fonts);
        self.initialized = true;
    }

    fn perform_search(&mut self) {
        if self.query.is_empty() {
            self.results.clear();
            return;
        }

        if let Some(ref store) = *self.store.lock() {
            let start = Instant::now();
            let query_lower = self.query.to_lowercase();

            self.results = store.iter_all()
                .filter(|e| {
                    let text = if self.match_path {
                        let mut p = String::new();
                        e.build_path(&mut p);
                        p
                    } else {
                        e.full_name().to_string()
                    };

                    if self.match_case {
                        text.contains(&self.query)
                    } else {
                        text.to_lowercase().contains(&query_lower)
                    }
                })
                .take(500)
                .cloned()
                .collect();
                
            self.search_time = start.elapsed().as_secs_f32() * 1000.0;
        }
    }
}

impl eframe::App for FSearchApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        if !self.initialized { self.setup_fonts(ctx); }

        // 检查扫描是否完成
        if self.is_scanning {
            if self.store.lock().is_some() {
                self.is_scanning = false;
            }
        }

        egui::TopBottomPanel::top("top").show(ctx, |ui| {
            ui.add_space(10.0);
            ui.horizontal(|ui| {
                let search_box = ui.add(
                    egui::TextEdit::singleline(&mut self.query)
                        .hint_text("Search...")
                        .desired_width(ui.available_width() - 150.0)
                );
                
                if search_box.changed() { self.perform_search(); }

                ui.checkbox(&mut self.match_case, "Aa");
                ui.checkbox(&mut self.match_path, "/");
            });
            ui.add_space(5.0);
        });

        egui::TopBottomPanel::bottom("bottom").show(ctx, |ui| {
            ui.horizontal(|ui| {
                if self.is_scanning {
                    ui.spinner();
                    ui.label("Indexing files... Please wait.");
                } else {
                    ui.label(format!("{} results ({:.1} ms)", self.results.len(), self.search_time));
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        let count = self.store.lock().as_ref().map(|s| s.total_count()).unwrap_or(0);
                        ui.label(format!("Total: {} items", count));
                    });
                }
            });
        });

        egui::CentralPanel::default().show(ctx, |ui| {
            if self.results.is_empty() && !self.query.is_empty() && !self.is_scanning {
                ui.centered_and_justified(|ui| { ui.label("No results found."); });
            } else {
                let text_height = egui::TextStyle::Body.resolve(ui.style()).size;
                TableBuilder::new(ui)
                    .striped(true)
                    .resizable(true)
                    .column(Column::initial(300.0).at_least(100.0))
                    .column(Column::remainder())
                    .column(Column::initial(100.0))
                    .header(25.0, |mut header| {
                        header.col(|ui| { ui.strong("Name"); });
                        header.col(|ui| { ui.strong("Path"); });
                        header.col(|ui| { ui.strong("Size"); });
                    })
                    .body(|body| {
                        body.rows(text_height + 4.0, self.results.len(), |mut row| {
                            let entry = &self.results[row.index()];
                            row.col(|ui| { ui.label(entry.full_name()); });
                            row.col(|ui| {
                                let mut p = String::new();
                                entry.build_path(&mut p);
                                ui.label(egui::RichText::new(p).color(egui::Color32::GRAY).small());
                            });
                            row.col(|ui| { ui.label(if entry.is_file() { format_size(entry.size()) } else { "-".to_owned() }); });
                        });
                    });
            }
        });
        
        if self.is_scanning { ctx.request_repaint(); }
    }
}

fn format_size(size: u64) -> String {
    if size < 1024 { return format!("{} B", size); }
    let kb = size as f64 / 1024.0;
    if kb < 1024.0 { return format!("{:.1} KB", kb); }
    let mb = kb / 1024.0;
    format!("{:.1} MB", mb)
}

pub fn run_gui(path: std::path::PathBuf) -> eframe::Result {
    let options = eframe::NativeOptions::default();
    eframe::run_native(
        "fsearch_rust",
        options,
        Box::new(|_cc| Ok(Box::new(FSearchApp::new(path)))),
    )
}