/* Cliente de la API local. Toda respuesta de error del servidor llega como
 * {error, message, details}; la convertimos en una excepción con esos campos
 * para que la interfaz pueda distinguir un conflicto de revisión de un fallo
 * de validación sin mirar el texto. */

export class ApiError extends Error {
  constructor(payload, status) {
    super(payload && payload.message ? payload.message : "Error de comunicación con Studio");
    this.code = (payload && payload.error) || "http_" + status;
    this.details = (payload && payload.details) || [];
    this.status = status;
  }
}

async function call(method, url, body) {
  let response;
  try {
    response = await fetch(url, {
      method,
      headers: body ? { "Content-Type": "application/json" } : undefined,
      body: body ? JSON.stringify(body) : undefined,
    });
  } catch (err) {
    throw new ApiError({ error: "offline", message: "No hay respuesta del servidor de Studio. ¿Se ha cerrado la ventana de consola?" }, 0);
  }
  const text = await response.text();
  let payload = null;
  if (text) { try { payload = JSON.parse(text); } catch { payload = null; } }
  if (!response.ok) throw new ApiError(payload, response.status);
  return payload;
}

export const api = {
  bootstrap: () => call("GET", "/api/bootstrap"),
  project: (id) => call("GET", "/api/projects/" + encodeURIComponent(id)),
  createProject: (payload) => call("POST", "/api/projects", payload),
  saveModel: (id, model) => call("PUT", "/api/projects/" + encodeURIComponent(id) + "/model", { model }),
  action: (id, action, extra = {}) =>
    call("POST", "/api/projects/" + encodeURIComponent(id) + "/actions", { action, ...extra }),
  readFile: (id, path) =>
    call("GET", "/api/projects/" + encodeURIComponent(id) + "/file?path=" + encodeURIComponent(path)),
  writeFile: (id, path, content, revision) =>
    call("PUT", "/api/projects/" + encodeURIComponent(id) + "/file", { path, content, revision }),

  /* Monitor serie. Degradan con elegancia: si el servidor no trae pyserial,
   * responde 503 y la vista lo explica en lugar de romperse. */
  ports: (project) => call("GET", "/api/serial/ports" + (project ? "?project=" + encodeURIComponent(project) : "")),
  connect: (payload) => call("POST", "/api/serial/connect", payload),
  disconnect: () => call("POST", "/api/serial/disconnect", {}),
  poll: (cursor) => call("GET", "/api/serial/poll?cursor=" + encodeURIComponent(cursor || 0)),
  send: (text) => call("POST", "/api/serial/send", { text }),
};
