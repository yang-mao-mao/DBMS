import { HttpDatabaseService } from "../services/HttpDatabaseService";
import { MockDatabaseService } from "../services/MockDatabaseService";

const jsonResponse = (body: unknown, status = 200): Response =>
  new Response(JSON.stringify(body), { status, headers: { "Content-Type": "application/json" } });

describe("database services", () => {
  it("validates and returns a real query response", async () => {
    const request = vi.fn(() => Promise.resolve(jsonResponse({
      success: true,
      columns: [{ name: "id", type: "INT64" }],
      rows: [[1]],
      affectedRows: 0,
      elapsedMs: 2,
      message: "ok",
    })));
    const service = new HttpDatabaseService("http://localhost:8765", request);
    const result = await service.execute("SELECT * FROM students;");
    expect(result.rows).toEqual([[1]]);
    expect(request).toHaveBeenCalledWith("http://localhost:8765/api/v1/query", expect.objectContaining({ method: "POST" }));
  });

  it("reports network failure", async () => {
    const request = vi.fn(() => Promise.reject(new Error("offline")));
    await expect(new HttpDatabaseService("http://localhost", request).healthCheck()).rejects.toThrow("unreachable");
  });

  it("rejects an invalid API response", async () => {
    const request = vi.fn(() => Promise.resolve(jsonResponse({ success: true })));
    await expect(new HttpDatabaseService("http://localhost", request).execute("SELECT 1")).rejects.toThrow("missing required fields");
  });

  it("returns Unsupported for CREATE INDEX in demo mode", async () => {
    const result = await new MockDatabaseService().execute("CREATE INDEX idx_id ON students(id);");
    expect(result.success).toBe(false);
    expect(result.error?.code).toBe("UNSUPPORTED");
  });

  it("does not split a semicolon inside a string", async () => {
    const result = await new MockDatabaseService().execute("INSERT INTO students VALUES (1, 'hello; world', 20);");
    expect(result.success).toBe(true);
    expect(result.affectedRows).toBe(1);
  });

  it("continues serving after an error", async () => {
    const service = new MockDatabaseService();
    expect((await service.execute("DELETE FROM students;")).success).toBe(false);
    expect((await service.execute("SELECT * FROM students;")).success).toBe(true);
  });
});
