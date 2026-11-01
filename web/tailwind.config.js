/** @type {import('tailwindcss').Config} */
export default {
  darkMode: "class",
  content: ["./index.html", "./src/**/*.{js,ts,jsx,tsx}"],
  theme: {
    extend: {
      fontFamily: {
        sans: ["Inter", "Segoe UI", "Microsoft YaHei UI", "sans-serif"],
        mono: ["JetBrains Mono", "Cascadia Code", "Consolas", "monospace"],
      },
      colors: {
        brand: "#5B8CFF",
      },
      boxShadow: {
        panel: "0 18px 45px rgba(0,0,0,.24)",
      },
    },
  },
  plugins: [],
};
