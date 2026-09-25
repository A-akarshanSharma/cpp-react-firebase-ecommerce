export const mediaUrl = (url) =>
  url?.startsWith('/media/')
    ? `${(import.meta.env.VITE_API_URL || '').replace(/\/$/, '')}${url}`
    : url
export async function prepareImage(file) {
  if (!file || !['image/png', 'image/jpeg', 'image/webp'].includes(file.type))
    throw new Error('Choose a PNG, JPEG or WebP image.')
  if (file.size > 4 * 1024 * 1024) throw new Error('Choose an image smaller than 4 MiB.')
  const bitmap = await createImageBitmap(file)
  try {
    const scale = Math.min(1, 2048 / Math.max(bitmap.width, bitmap.height))
    const canvas = document.createElement('canvas')
    canvas.width = Math.max(1, Math.round(bitmap.width * scale))
    canvas.height = Math.max(1, Math.round(bitmap.height * scale))
    canvas.getContext('2d').drawImage(bitmap, 0, 0, canvas.width, canvas.height)
    const blob = await new Promise((resolve) => canvas.toBlob(resolve, 'image/png'))
    if (!blob || blob.size > 4 * 1024 * 1024)
      throw new Error('The prepared image is too large. Choose a smaller image.')
    return blob
  } finally {
    bitmap.close()
  }
}
