function applyOrbit() {
  const composition = app.project.activeItem;
  const layers = composition ? composition.selectedLayers : [];
  const model = layers.length === 1 ? layers[0] : null;
  if (!model || model.type !== "model3d" || model.locked) {
    app.alert(app.t("select"));
    return;
  }

  const key = "aurea.3d.replicator";
  const repeater = model.effect(key) || model.addEffect(key);
  if (!repeater) {
    app.alert(app.t("failed"));
    return;
  }
  repeater.enabled = true;
  function write(name, value) {
    const parameter = repeater.property(name);
    if (parameter.isTimeVarying) parameter.setValueAtTime(composition.time, value);
    else parameter.setValue(value);
  }
  write("count", Math.round(ui.get("copies")));
  write("layout", 2);
  write("radius", ui.get("radius"));
}
