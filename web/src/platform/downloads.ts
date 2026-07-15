import { Capacitor, registerPlugin } from '@capacitor/core'

interface DownloadsPlugin {
  saveText(options: { fileName: string; mimeType: string; content: string }): Promise<{ uri: string }>
}

const Downloads = registerPlugin<DownloadsPlugin>('Downloads')

export async function saveTextDownload(fileName: string, mimeType: string, content: string): Promise<string> {
  if (Capacitor.isNativePlatform()) {
    await Downloads.saveText({ fileName, mimeType, content })
    return `Сохранено в Download/C6 Tracker: ${fileName}`
  }

  const blob = new Blob([content], { type: mimeType })
  const url = URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.href = url
  link.download = fileName
  document.body.appendChild(link)
  link.click()
  link.remove()
  window.setTimeout(() => URL.revokeObjectURL(url), 1000)
  return `Скачан файл: ${fileName}`
}
