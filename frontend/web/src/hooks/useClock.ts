import { useEffect, useState } from "react";

function formatClock(date: Date) {
  return date.toLocaleString("zh-CN", {
    hour12: false,
    year: "numeric",
    month: "2-digit",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
}

export function useClock() {
  const [time, setTime] = useState(() => formatClock(new Date()));

  useEffect(() => {
    const timer = window.setInterval(() => {
      setTime(formatClock(new Date()));
    }, 1000);

    return () => window.clearInterval(timer);
  }, []);

  return time;
}
