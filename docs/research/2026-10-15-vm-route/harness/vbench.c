// Guest driver for the VM host's benchmark device (virtio ID 63): /dev/vbench maps its shared memory region and
// times doorbell round trips (ioctl 1: interrupt-driven, ioctl 2: polled; argument: count, returns total ns).
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/completion.h>
#include <linux/scatterlist.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
static struct virtqueue *vq;
static struct virtio_shm_region shm;
static DECLARE_COMPLETION(done);
static u64 *buf;
static DEFINE_MUTEX(lock);
static void vq_cb(struct virtqueue *q) { complete(&done); }
static long ping(u64 n, int poll) {
  ktime_t t0 = ktime_get();
  unsigned int len;
  if (poll) virtqueue_disable_cb(vq); else virtqueue_enable_cb(vq);
  for (u64 i = 0; i < n; i++) {
    struct scatterlist out, in, *sgs[2] = {&out, &in};
    buf[0] = i;
    sg_init_one(&out, &buf[0], 8);
    sg_init_one(&in, &buf[1], 8);
    reinit_completion(&done);
    if (virtqueue_add_sgs(vq, sgs, 1, 1, buf, GFP_KERNEL)) return -EIO;
    virtqueue_kick(vq);
    if (poll) { while (!virtqueue_get_buf(vq, &len)) cpu_relax(); }
    else { wait_for_completion(&done); while (!virtqueue_get_buf(vq, &len)) cpu_relax(); }
    if (buf[1] != i + 1) return -EIO;
  }
  return ktime_to_ns(ktime_sub(ktime_get(), t0));
}
static long vb_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
  long r;
  if (cmd != 1 && cmd != 2) return -EINVAL;
  mutex_lock(&lock); r = ping(arg, cmd == 2); mutex_unlock(&lock);
  return r < 0 ? r : (r / 1000);  // microseconds (an ioctl returns int)
}
static int vb_mmap(struct file *f, struct vm_area_struct *vma) {
  unsigned long len = vma->vm_end - vma->vm_start;
  if (len > shm.len) return -EINVAL;
  return remap_pfn_range(vma, vma->vm_start, shm.addr >> PAGE_SHIFT, len, vma->vm_page_prot);
}
static const struct file_operations fops = {.owner = THIS_MODULE, .unlocked_ioctl = vb_ioctl, .mmap = vb_mmap};
static struct miscdevice misc = {.minor = MISC_DYNAMIC_MINOR, .name = "vbench", .fops = &fops, .mode = 0666};
static int vb_probe(struct virtio_device *vdev) {
  vq = virtio_find_single_vq(vdev, vq_cb, "doorbell");
  if (IS_ERR(vq)) return PTR_ERR(vq);
  if (!virtio_get_shm_region(vdev, &shm, 0)) { dev_err(&vdev->dev, "no shared memory region\n"); return -ENODEV; }
  buf = kmalloc(16, GFP_KERNEL);
  virtio_device_ready(vdev);
  dev_info(&vdev->dev, "vbench: shm %#llx + %#llx\n", shm.addr, shm.len);
  return misc_register(&misc);
}
static void vb_remove(struct virtio_device *vdev) { misc_deregister(&misc); virtio_reset_device(vdev); vdev->config->del_vqs(vdev); kfree(buf); }
static const struct virtio_device_id ids[] = {{63, VIRTIO_DEV_ANY_ID}, {0}};
static struct virtio_driver drv = {.driver.name = "vbench", .id_table = ids, .probe = vb_probe, .remove = vb_remove};
module_virtio_driver(drv);
MODULE_DEVICE_TABLE(virtio, ids);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VM-route transport benchmark device");
